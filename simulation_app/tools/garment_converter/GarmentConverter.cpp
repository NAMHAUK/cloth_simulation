#include "GarmentConverter.h"

#include "asset/AssetIO.h"
#include "asset/MeshGeometryUtils.h"
#include "utils/NumericUtils.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
constexpr float obj_to_world_scale = 0.01f;

using MeshEdgeBuilder = std::vector<MeshEdge> (*)(std::uint32_t, const std::vector<std::uint32_t>&);

GarmentDistanceConstraints build_distance_constraints(const std::vector<std::uint32_t>& triangle_indices,
                                                      const std::vector<float>& vertices,
                                                      MeshEdgeBuilder build_edges)
{
    const auto vertex_count = static_cast<std::uint32_t>(vertices.size() / asset_io::position_components);
    const std::vector<MeshEdge> edges = build_edges(vertex_count, triangle_indices);
    ColorizedMeshEdges colorized_edges = colorize_mesh_edges(vertex_count, edges);

    GarmentDistanceConstraints distance_constraints;
    distance_constraints.colorized_edges = std::move(colorized_edges.edges);
    distance_constraints.color_ranges = std::move(colorized_edges.ranges);
    distance_constraints.rest_lengths = compute_mesh_edge_lengths(distance_constraints.colorized_edges, vertices);
    return distance_constraints;
}

GarmentDistanceConstraints build_stretch_constraints(const std::vector<std::uint32_t>& triangle_indices,
                                                     const std::vector<float>& vertices)
{
    return build_distance_constraints(triangle_indices, vertices, build_unique_triangle_edges);
}

GarmentDistanceConstraints build_bending_constraints(const std::vector<std::uint32_t>& triangle_indices,
                                                     const std::vector<float>& vertices)
{
    return build_distance_constraints(triangle_indices, vertices, build_unique_bending_edges);
}

void add_undirected_edge(std::vector<std::vector<std::uint32_t>>& vertex_edges, std::uint32_t vertex_a, std::uint32_t vertex_b)
{
    vertex_edges[vertex_a].push_back(vertex_b);
    vertex_edges[vertex_b].push_back(vertex_a);
}

void assign_bounds(GarmentMesh& garment_mesh, const glm::vec3& min_bounds, const glm::vec3& max_bounds)
{
    garment_mesh.bounds_center = (min_bounds + max_bounds) * 0.5f;
    garment_mesh.bounds_radius = glm::length(max_bounds - min_bounds) * 0.5f;
}

std::uint32_t count_connected_components(const std::vector<std::vector<std::uint32_t>>& vertex_edges,
                                         const std::vector<std::uint8_t>& used_vertices)
{
    std::uint32_t component_count = 0;
    std::vector<std::uint8_t> visited(used_vertices.size(), 0u);
    std::vector<std::uint32_t> stack;

    for (std::uint32_t vertex_index = 0; vertex_index < used_vertices.size(); ++vertex_index) {
        if (used_vertices[vertex_index] == 0u || visited[vertex_index] != 0u) {
            continue;
        }

        ++component_count;
        visited[vertex_index] = 1u;
        stack.push_back(vertex_index);

        while (!stack.empty()) {
            const std::uint32_t current_vertex = stack.back();
            stack.pop_back();

            for (std::uint32_t next_vertex : vertex_edges[current_vertex]) {
                if (visited[next_vertex] != 0u) {
                    continue;
                }
                visited[next_vertex] = 1u;
                stack.push_back(next_vertex);
            }
        }
    }

    return component_count;
}

bool validate_garment_obj(const GarmentMesh& garment_mesh, std::uint32_t vertex_count)
{
    if (vertex_count == 0 ||
        garment_mesh.vertices.size() != static_cast<std::size_t>(vertex_count) * asset_io::position_components) {
        std::cerr << "Invalid garment OBJ vertex data.\n";
        return false;
    }

    if (garment_mesh.indices.empty() || garment_mesh.indices.size() % 3u != 0u) {
        std::cerr << "Invalid garment OBJ triangle data.\n";
        return false;
    }

    std::vector<std::uint8_t> used_vertices(vertex_count, 0u);
    std::vector<std::vector<std::uint32_t>> vertex_edges(vertex_count);

    for (std::uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        if (!is_finite_vec3(get_vertex_position(garment_mesh.vertices, vertex_index))) {
            std::cerr << "Garment OBJ contains a non-finite vertex.\n";
            return false;
        }
    }

    for (std::size_t index = 0; index < garment_mesh.indices.size(); index += 3u) {
        const std::uint32_t vertex_a = garment_mesh.indices[index];
        const std::uint32_t vertex_b = garment_mesh.indices[index + 1u];
        const std::uint32_t vertex_c = garment_mesh.indices[index + 2u];

        if (vertex_a >= vertex_count || vertex_b >= vertex_count || vertex_c >= vertex_count) {
            std::cerr << "Garment OBJ contains an out-of-range face index.\n";
            return false;
        }

        if (vertex_a == vertex_b || vertex_b == vertex_c || vertex_c == vertex_a) {
            std::cerr << "Garment OBJ contains a degenerate triangle.\n";
            return false;
        }

        const glm::vec3 position_a = get_vertex_position(garment_mesh.vertices, vertex_a);
        const glm::vec3 position_b = get_vertex_position(garment_mesh.vertices, vertex_b);
        const glm::vec3 position_c = get_vertex_position(garment_mesh.vertices, vertex_c);
        if (glm::length(glm::cross(position_b - position_a, position_c - position_a)) <= 1.0e-10f) {
            std::cerr << "Garment OBJ contains a zero-area triangle.\n";
            return false;
        }

        used_vertices[vertex_a] = 1u;
        used_vertices[vertex_b] = 1u;
        used_vertices[vertex_c] = 1u;

        add_undirected_edge(vertex_edges, vertex_a, vertex_b);
        add_undirected_edge(vertex_edges, vertex_b, vertex_c);
        add_undirected_edge(vertex_edges, vertex_c, vertex_a);
    }

    std::uint32_t unused_vertex_count = 0;
    for (std::uint8_t used : used_vertices) {
        if (used == 0u) {
            ++unused_vertex_count;
        }
    }
    if (unused_vertex_count > 0u) {
        std::cerr << "Garment OBJ contains unused vertices: " << unused_vertex_count << '\n';
        return false;
    }

    const std::uint32_t component_count = count_connected_components(vertex_edges, used_vertices);
    if (component_count != 1u) {
        std::cerr << "Garment OBJ must be one connected component. component_count=" << component_count << '\n';
        return false;
    }

    if (!is_finite_vec3(garment_mesh.bounds_center) || !std::isfinite(garment_mesh.bounds_radius) ||
        garment_mesh.bounds_radius <= 0.0f) {
        std::cerr << "Invalid garment OBJ bounds.\n";
        return false;
    }

    return true;
}

// parse //
bool parse_face_token(const std::string& token, std::uint32_t vertex_count, std::uint32_t& index)
{
    const auto token_view{token};
    const auto vertex_text = token_view.substr(0, token_view.find('/'));
    if (vertex_text.empty()) {
        return false;
    }

    std::uint32_t parsed_index = 0;
    const char* text_begin = vertex_text.data();
    const char* text_end = text_begin + vertex_text.size();
    const auto [parsed_end, error] = std::from_chars(text_begin, text_end, parsed_index);

    if (error != std::errc{} || parsed_end != text_end || parsed_index == 0 || parsed_index > vertex_count) {
        return false;
    }

    index = parsed_index - 1u;
    return true;
}

bool parse_vertex_line(std::istringstream& line_stream,
                       GarmentMesh& garment_mesh,
                       glm::vec3& min_bounds,
                       glm::vec3& max_bounds,
                       std::uint32_t& vertex_count)
{
    glm::vec3 vertex{};
    if (!(line_stream >> vertex.x >> vertex.y >> vertex.z)) {
        return false;
    }

    vertex *= obj_to_world_scale;
    if (!is_finite_vec3(vertex)) {
        return false;
    }

    garment_mesh.vertices.push_back(vertex.x);
    garment_mesh.vertices.push_back(vertex.y);
    garment_mesh.vertices.push_back(vertex.z);

    min_bounds = glm::min(min_bounds, vertex);
    max_bounds = glm::max(max_bounds, vertex);

    ++vertex_count;
    return true;
}

bool parse_face_line(std::istringstream& line_stream,
                     std::uint32_t vertex_count,
                     GarmentMesh& garment_mesh)
{
    std::array<std::uint32_t, 3> face_indices{};
    std::string face_token;
    std::size_t face_vertex_count = 0;

    while (line_stream >> face_token) {
        if (face_vertex_count >= face_indices.size()) {
            return false;
        }

        std::uint32_t index = 0;
        if (!parse_face_token(face_token, vertex_count, index)) {
            return false;
        }
        face_indices[face_vertex_count] = index;
        ++face_vertex_count;
    }

    if (face_vertex_count != face_indices.size()) {
        return false;
    }

    garment_mesh.indices.push_back(face_indices[0]);
    garment_mesh.indices.push_back(face_indices[1]);
    garment_mesh.indices.push_back(face_indices[2]);
    return true;
}

bool read_obj_mesh_lines(std::istream& input,
                         GarmentMesh& garment_mesh,
                         glm::vec3& min_bounds,
                         glm::vec3& max_bounds,
                         std::uint32_t& vertex_count)
{
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream line_stream(line);
        std::string tag;
        line_stream >> tag;

        if (tag.empty() || tag[0] == '#') {
            continue;
        }

        if (tag == "v") {
            if (!parse_vertex_line(line_stream, garment_mesh, min_bounds, max_bounds, vertex_count)) {
                std::cerr << "Invalid garment OBJ vertex.\n";
                return false;
            }
        } else if (tag == "f") {
            if (!parse_face_line(line_stream, vertex_count, garment_mesh)) {
                std::cerr << "Invalid garment OBJ face. Only triangle faces are supported.\n";
                return false;
            }
        }
    }

    return true;
}

bool build_garment_simulation_data(GarmentMesh& garment_mesh, std::uint32_t vertex_count)
{
    if (!build_vertex_face_adjacency(vertex_count, garment_mesh.indices, garment_mesh.adjacency)) {
        std::cerr << "Invalid garment OBJ topology.\n";
        return false;
    }

    garment_mesh.stretch_constraints = build_stretch_constraints(garment_mesh.indices, garment_mesh.vertices);
    if (!garment_mesh.stretch_constraints.is_valid()) {
        std::cerr << "Invalid garment stretch constraints.\n";
        return false;
    }

    garment_mesh.bending_constraints = build_bending_constraints(garment_mesh.indices, garment_mesh.vertices);
    if (!garment_mesh.bending_constraints.is_valid()) {
        std::cerr << "Invalid garment bending constraints.\n";
        return false;
    }

    return true;
}

void print_garment_obj_summary(const std::filesystem::path& obj_path, const GarmentMesh& garment_mesh)
{
    std::cout << "Read garment OBJ: " << obj_path << '\n';
    std::cout << "  vertices=" << garment_mesh.vertices.size() / asset_io::position_components
              << " triangles=" << garment_mesh.indices.size() / 3u
              << " stretch_constraints=" << garment_mesh.stretch_constraints.colorized_edges.size()
              << " bending_constraints=" << garment_mesh.bending_constraints.colorized_edges.size()
              << " bounds_radius=" << garment_mesh.bounds_radius << '\n';
}
}

bool read_garment_obj(const std::filesystem::path& obj_path, GarmentMesh& garment_mesh)
{
    const auto fail = [](const char* message) {
        std::cerr << message << '\n';
        return false;
    };

    std::ifstream input(obj_path);
    if (!input) {
        return fail("Failed to open garment OBJ.");
    }

    GarmentMesh next_mesh;
    glm::vec3 min_bounds{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    };
    glm::vec3 max_bounds{
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()
    };
    std::uint32_t vertex_count = 0;

    if (!read_obj_mesh_lines(input, next_mesh, min_bounds, max_bounds, vertex_count)) {
        return false;
    }

    if (next_mesh.vertices.empty() || next_mesh.indices.empty()) {
        return fail("Empty garment OBJ mesh.");
    }

    assign_bounds(next_mesh, min_bounds, max_bounds);
    if (!validate_garment_obj(next_mesh, vertex_count)) {
        return false;
    }

    if (!build_garment_simulation_data(next_mesh, vertex_count)) {
        return false;
    }

    print_garment_obj_summary(obj_path, next_mesh);

    garment_mesh = std::move(next_mesh);
    return true;
}

bool write_garment_asset(const std::filesystem::path& garment_asset_path, const GarmentMesh& garment_mesh)
{
    if (!asset_io::write_garment_asset(garment_asset_path, garment_mesh)) {
        return false;
    }

    std::cout << "Wrote garment asset: " << garment_asset_path << '\n';
    return true;
}
