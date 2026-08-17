#include "GarmentConverter.h"

#include "asset/AssetIO.h"
#include "asset/MeshGeometryUtils.h"
#include "utils/NumericUtils.h"

#include <algorithm>
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
constexpr float obj_to_world_scale = 0.001f;
constexpr float waistband_attachment_band_height = 0.03f;

using MeshEdgeBuilder = std::vector<MeshEdge> (*)(std::uint32_t, const std::vector<std::uint32_t>&);

MeshEdge make_sorted_edge(std::uint32_t vertex_a, std::uint32_t vertex_b)
{
    if (vertex_a < vertex_b) {
        return {vertex_a, vertex_b};
    }
    return {vertex_b, vertex_a};
}

bool is_less_edge(const MeshEdge& lhs, const MeshEdge& rhs)
{
    if (lhs.vertex_a != rhs.vertex_a) {
        return lhs.vertex_a < rhs.vertex_a;
    }
    return lhs.vertex_b < rhs.vertex_b;
}

bool is_same_edge(const MeshEdge& lhs, const MeshEdge& rhs)
{
    return lhs.vertex_a == rhs.vertex_a && lhs.vertex_b == rhs.vertex_b;
}

GarmentDistanceConstraints build_distance_constraints(const std::vector<std::uint32_t>& triangle_indices,
                                                      const std::vector<float>& vertices,
                                                      MeshEdgeBuilder build_edges)
{
    const auto vertex_count = static_cast<std::uint32_t>(vertices.size() / position_components);
    const std::vector<MeshEdge> edges = build_edges(vertex_count, triangle_indices);
    ColorizedMeshEdges colorized_edges = colorize_mesh_edges(vertex_count, edges);

    GarmentDistanceConstraints distance_constraints;
    distance_constraints.colorized_edges = std::move(colorized_edges.edges);
    distance_constraints.color_ranges = std::move(colorized_edges.ranges);
    distance_constraints.rest_lengths =
        compute_mesh_edge_lengths(distance_constraints.colorized_edges, vertices);
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

std::vector<MeshEdge> build_boundary_edges(const std::vector<std::uint32_t>& triangle_indices,
                                           std::uint32_t vertex_count)
{
    if (vertex_count == 0u || triangle_indices.empty() || triangle_indices.size() % 3u != 0u) {
        return {};
    }

    std::vector<MeshEdge> edges;
    edges.reserve(triangle_indices.size());
    for (std::size_t index = 0; index < triangle_indices.size(); index += 3u) {
        const std::uint32_t vertex_a = triangle_indices[index];
        const std::uint32_t vertex_b = triangle_indices[index + 1u];
        const std::uint32_t vertex_c = triangle_indices[index + 2u];
        if (vertex_a >= vertex_count || vertex_b >= vertex_count || vertex_c >= vertex_count) {
            return {};
        }

        edges.push_back(make_sorted_edge(vertex_a, vertex_b));
        edges.push_back(make_sorted_edge(vertex_b, vertex_c));
        edges.push_back(make_sorted_edge(vertex_c, vertex_a));
    }

    std::sort(edges.begin(), edges.end(), is_less_edge);

    std::vector<MeshEdge> boundary_edges;
    for (std::size_t edge_begin = 0; edge_begin < edges.size();) {
        std::size_t edge_end = edge_begin + 1u;
        while (edge_end < edges.size() && is_same_edge(edges[edge_begin], edges[edge_end])) {
            ++edge_end;
        }

        if (edge_end - edge_begin == 1u) {
            boundary_edges.push_back(edges[edge_begin]);
        }

        edge_begin = edge_end;
    }

    return boundary_edges;
}

std::vector<std::vector<std::uint32_t>> find_boundary_loops(const GarmentMesh& garment_mesh)
{
    const auto vertex_count = static_cast<std::uint32_t>(garment_mesh.vertices.size() / position_components);
    const std::vector<MeshEdge> boundary_edges =
        build_boundary_edges(garment_mesh.triangle_vertex_indices, vertex_count);
    if (boundary_edges.empty()) {
        return {};
    }

    std::vector<std::vector<std::uint32_t>> neighbors(vertex_count);
    for (const MeshEdge& edge : boundary_edges) {
        neighbors[edge.vertex_a].push_back(edge.vertex_b);
        neighbors[edge.vertex_b].push_back(edge.vertex_a);
    }

    std::vector<std::uint8_t> visited(vertex_count, 0u);
    std::vector<std::vector<std::uint32_t>> loops;
    for (std::uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        if (visited[vertex_index] != 0u || neighbors[vertex_index].empty()) {
            continue;
        }

        std::vector<std::uint32_t> loop_vertices;
        std::vector<std::uint32_t> stack{vertex_index};
        visited[vertex_index] = 1u;
        while (!stack.empty()) {
            const std::uint32_t current_vertex = stack.back();
            stack.pop_back();
            loop_vertices.push_back(current_vertex);

            for (std::uint32_t next_vertex : neighbors[current_vertex]) {
                if (visited[next_vertex] != 0u) {
                    continue;
                }

                visited[next_vertex] = 1u;
                stack.push_back(next_vertex);
            }
        }

        if (!loop_vertices.empty()) {
            loops.push_back(std::move(loop_vertices));
        }
    }

    return loops;
}

float average_loop_y(const std::vector<std::uint32_t>& loop_vertices, const std::vector<float>& vertices)
{
    float y_sum = 0.0f;
    for (std::uint32_t vertex_index : loop_vertices) {
        y_sum += get_vertex_position(vertices, vertex_index).y;
    }
    return y_sum / static_cast<float>(loop_vertices.size());
}

std::vector<std::uint32_t> build_waistband_attachment_vertex_indices(const GarmentMesh& garment_mesh)
{
    const auto vertex_count = static_cast<std::uint32_t>(garment_mesh.vertices.size() / position_components);
    if (vertex_count == 0u) {
        return {};
    }

    float max_y = std::numeric_limits<float>::lowest();
    for (std::uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        max_y = std::max(max_y, get_vertex_position(garment_mesh.vertices, vertex_index).y);
    }

    const float attachment_min_y = max_y - waistband_attachment_band_height;
    std::vector<std::uint32_t> attachment_vertices;
    for (std::uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        if (get_vertex_position(garment_mesh.vertices, vertex_index).y >= attachment_min_y) {
            attachment_vertices.push_back(vertex_index);
        }
    }
    return attachment_vertices;
}

void assign_bounds(GarmentMesh& garment_mesh, const glm::vec3& min_bounds, const glm::vec3& max_bounds)
{
    garment_mesh.bounds_center = (min_bounds + max_bounds) * 0.5f;
    garment_mesh.bounds_radius = glm::length(max_bounds - min_bounds) * 0.5f;
}

std::uint32_t find_component_root(std::vector<std::uint32_t>& component_parent, std::uint32_t vertex_index)
{
    if (component_parent[vertex_index] != vertex_index) {
        component_parent[vertex_index] =
            find_component_root(component_parent, component_parent[vertex_index]);
    }

    return component_parent[vertex_index];
}

bool validate_garment_obj(const GarmentMesh& garment_mesh, std::uint32_t vertex_count)
{
    if (vertex_count == 0 ||
        garment_mesh.vertices.size() != static_cast<std::size_t>(vertex_count) * position_components) {
        std::cerr << "Invalid garment OBJ vertex data.\n";
        return false;
    }

    if (garment_mesh.triangle_vertex_indices.empty() ||
        garment_mesh.triangle_vertex_indices.size() % 3u != 0u) {
        std::cerr << "Invalid garment OBJ triangle data.\n";
        return false;
    }

    std::vector<std::uint8_t> used_vertices(vertex_count, 0u);
    std::vector<std::uint32_t> component_parent(vertex_count, 0u);
    for (std::uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        component_parent[vertex_index] = vertex_index;
        if (!is_finite_vec3(get_vertex_position(garment_mesh.vertices, vertex_index))) {
            std::cerr << "Garment OBJ contains a non-finite vertex.\n";
            return false;
        }
    }

    for (std::size_t index = 0; index < garment_mesh.triangle_vertex_indices.size(); index += 3u) {
        const std::uint32_t vertex_a = garment_mesh.triangle_vertex_indices[index];
        const std::uint32_t vertex_b = garment_mesh.triangle_vertex_indices[index + 1u];
        const std::uint32_t vertex_c = garment_mesh.triangle_vertex_indices[index + 2u];

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

        const std::uint32_t root_a = find_component_root(component_parent, vertex_a);
        component_parent[find_component_root(component_parent, vertex_b)] = root_a;
        component_parent[find_component_root(component_parent, vertex_c)] = root_a;
    }

    std::uint32_t unused_vertex_count = 0;
    std::uint32_t component_count = 0;
    std::vector<std::uint8_t> component_roots(vertex_count, 0u);
    for (std::uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        if (used_vertices[vertex_index] == 0u) {
            ++unused_vertex_count;
            continue;
        }

        const std::uint32_t root = find_component_root(component_parent, vertex_index);
        if (component_roots[root] != 0u) {
            continue;
        }
        component_roots[root] = 1u;
        ++component_count;
    }

    if (unused_vertex_count > 0u) {
        std::cerr << "Garment OBJ contains unused vertices: " << unused_vertex_count << '\n';
        return false;
    }

    if (component_count != 1u) {
        std::cerr << "Garment OBJ must be one connected component. component_count=" << component_count
                  << '\n';
        return false;
    }

    if (!is_finite_vec3(garment_mesh.bounds_center) ||
        !std::isfinite(garment_mesh.bounds_radius) ||
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

bool parse_face_line(std::istringstream& line_stream, std::uint32_t vertex_count, GarmentMesh& garment_mesh)
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

    garment_mesh.triangle_vertex_indices.push_back(face_indices[0]);
    garment_mesh.triangle_vertex_indices.push_back(face_indices[1]);
    garment_mesh.triangle_vertex_indices.push_back(face_indices[2]);
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

bool build_garment_simulation_data(GarmentMesh& garment_mesh)
{
    garment_mesh.stretch_constraints =
        build_stretch_constraints(garment_mesh.triangle_vertex_indices, garment_mesh.vertices);
    if (!garment_mesh.stretch_constraints.is_valid()) {
        std::cerr << "Invalid garment stretch constraints.\n";
        return false;
    }

    garment_mesh.bending_constraints =
        build_bending_constraints(garment_mesh.triangle_vertex_indices, garment_mesh.vertices);
    if (!garment_mesh.bending_constraints.is_valid()) {
        std::cerr << "Invalid garment bending constraints.\n";
        return false;
    }

    if (garment_mesh.garment_category == GarmentCategory::Bottom) {
        garment_mesh.attachment_vertex_indices = build_waistband_attachment_vertex_indices(garment_mesh);
        if (garment_mesh.attachment_vertex_indices.empty()) {
            std::cerr << "Cannot build waistband attachment vertices.\n";
            return false;
        }
    }

    return true;
}

void print_garment_obj_summary(const std::filesystem::path& obj_path, const GarmentMesh& garment_mesh)
{
    std::cout << "Read garment OBJ: " << obj_path << '\n';
    std::cout << "  vertices=" << garment_mesh.vertices.size() / position_components
              << " triangles=" << garment_mesh.triangle_vertex_indices.size() / 3u
              << " stretch_constraints=" << garment_mesh.stretch_constraints.colorized_edges.size()
              << " bending_constraints=" << garment_mesh.bending_constraints.colorized_edges.size()
              << " attachment_vertices=" << garment_mesh.attachment_vertex_indices.size()
              << " bounds_radius=" << garment_mesh.bounds_radius << '\n';
}
}

bool read_garment_obj(const std::filesystem::path& obj_path,
                      GarmentCategory garment_category,
                      GarmentMesh& garment_mesh)
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
    next_mesh.garment_category = garment_category;
    glm::vec3 min_bounds{std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max()};
    glm::vec3 max_bounds{std::numeric_limits<float>::lowest(),
                         std::numeric_limits<float>::lowest(),
                         std::numeric_limits<float>::lowest()};
    std::uint32_t vertex_count = 0;

    if (!read_obj_mesh_lines(input, next_mesh, min_bounds, max_bounds, vertex_count)) {
        return false;
    }

    if (next_mesh.vertices.empty() || next_mesh.triangle_vertex_indices.empty()) {
        return fail("Empty garment OBJ mesh.");
    }

    assign_bounds(next_mesh, min_bounds, max_bounds);
    if (!validate_garment_obj(next_mesh, vertex_count)) {
        return false;
    }

    std::uint32_t flipped_triangle_count = 0u;
    if (!orient_triangle_winding_outward(vertex_count,
                                         next_mesh.vertices,
                                         next_mesh.bounds_center,
                                         next_mesh.triangle_vertex_indices,
                                         flipped_triangle_count)) {
        return fail("Cannot orient garment triangle winding consistently.");
    }
    if (flipped_triangle_count > 0u) {
        std::cout << "Oriented garment triangle winding: flipped " << flipped_triangle_count
                  << " triangles.\n";
    }

    if (!build_garment_simulation_data(next_mesh)) {
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
