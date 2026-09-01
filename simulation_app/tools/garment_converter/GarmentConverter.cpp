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
#include <stdexcept>
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

GarmentDistanceConstraints build_distance_constraints(const std::vector<std::uint32_t>& triangle_indices,
                                                      const std::vector<float>& vertices,
                                                      MeshEdgeBuilder build_edges)
{
    const auto vertex_count = static_cast<std::uint32_t>(vertices.size() / position_components);
    const std::vector<MeshEdge> edges = build_edges(vertex_count, triangle_indices);
    ColorizedMeshEdges colorized_edges = colorize_mesh_edges(vertex_count, edges);

    GarmentDistanceConstraints distance_constraints;
    distance_constraints.colorized_edges = std::move(colorized_edges.edges);
    distance_constraints.color_states = std::move(colorized_edges.color_states);
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

void validate_garment_obj(const GarmentMesh& garment_mesh, std::uint32_t vertex_count)
{
    if (vertex_count == 0 || garment_mesh.vertices.size() != vertex_count * position_components) {
        throw std::runtime_error("Invalid garment OBJ vertex data.");
    }

    if (garment_mesh.triangle_vertex_indices.empty() ||
        garment_mesh.triangle_vertex_indices.size() % 3u != 0u) {
        throw std::runtime_error("Invalid garment OBJ triangle data.");
    }

    std::vector<std::uint8_t> used_vertices(vertex_count, 0u);
    std::vector<std::uint32_t> component_parent(vertex_count, 0u);
    for (std::uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        component_parent[vertex_index] = vertex_index;
        if (!is_finite_vec3(get_vertex_position(garment_mesh.vertices, vertex_index))) {
            throw std::runtime_error("Garment OBJ contains a non-finite vertex.");
        }
    }

    for (std::size_t index = 0; index < garment_mesh.triangle_vertex_indices.size(); index += 3u) {
        const std::uint32_t vertex_a = garment_mesh.triangle_vertex_indices[index];
        const std::uint32_t vertex_b = garment_mesh.triangle_vertex_indices[index + 1u];
        const std::uint32_t vertex_c = garment_mesh.triangle_vertex_indices[index + 2u];

        if (vertex_a >= vertex_count || vertex_b >= vertex_count || vertex_c >= vertex_count) {
            throw std::runtime_error("Garment OBJ contains an out-of-range face index.");
        }

        if (vertex_a == vertex_b || vertex_b == vertex_c || vertex_c == vertex_a) {
            throw std::runtime_error("Garment OBJ contains a degenerate triangle.");
        }

        const glm::vec3 position_a = get_vertex_position(garment_mesh.vertices, vertex_a);
        const glm::vec3 position_b = get_vertex_position(garment_mesh.vertices, vertex_b);
        const glm::vec3 position_c = get_vertex_position(garment_mesh.vertices, vertex_c);
        if (glm::length(glm::cross(position_b - position_a, position_c - position_a)) <= 1.0e-10f) {
            throw std::runtime_error("Garment OBJ contains a zero-area triangle.");
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
        throw std::runtime_error("Garment OBJ contains unused vertices: " +
                                 std::to_string(unused_vertex_count));
    }

    if (component_count != 1u) {
        throw std::runtime_error("Garment OBJ must be one connected component. component_count=" +
                                 std::to_string(component_count));
    }

    if (!is_finite_vec3(garment_mesh.bounds_center) ||
        !std::isfinite(garment_mesh.bounds_radius) ||
        garment_mesh.bounds_radius <= 0.0f) {
        throw std::runtime_error("Invalid garment OBJ bounds.");
    }
}

// parse //
std::uint32_t parse_face_token(const std::string& token, std::uint32_t vertex_count)
{
    const auto token_view{token};
    const auto vertex_text = token_view.substr(0, token_view.find('/'));
    if (vertex_text.empty()) {
        throw std::runtime_error("Invalid garment OBJ face. Only triangle faces are supported.");
    }

    std::uint32_t parsed_index = 0;
    const char* text_begin = vertex_text.data();
    const char* text_end = text_begin + vertex_text.size();
    const auto [parsed_end, error] = std::from_chars(text_begin, text_end, parsed_index);

    if (error != std::errc{} || parsed_end != text_end || parsed_index == 0 || parsed_index > vertex_count) {
        throw std::runtime_error("Invalid garment OBJ face. Only triangle faces are supported.");
    }

    return parsed_index - 1u;
}

void parse_vertex_line(std::istringstream& line_stream,
                       GarmentMesh& garment_mesh,
                       glm::vec3& min_bounds,
                       glm::vec3& max_bounds,
                       std::uint32_t& vertex_count)
{
    glm::vec3 vertex{};
    if (!(line_stream >> vertex.x >> vertex.y >> vertex.z)) {
        throw std::runtime_error("Invalid garment OBJ vertex.");
    }

    vertex *= obj_to_world_scale;
    if (!is_finite_vec3(vertex)) {
        throw std::runtime_error("Invalid garment OBJ vertex.");
    }

    garment_mesh.vertices.push_back(vertex.x);
    garment_mesh.vertices.push_back(vertex.y);
    garment_mesh.vertices.push_back(vertex.z);

    min_bounds = glm::min(min_bounds, vertex);
    max_bounds = glm::max(max_bounds, vertex);

    ++vertex_count;
}

void parse_face_line(std::istringstream& line_stream, std::uint32_t vertex_count, GarmentMesh& garment_mesh)
{
    std::array<std::uint32_t, 3> face_indices{};
    std::string face_token;
    std::size_t face_vertex_count = 0;

    while (line_stream >> face_token) {
        if (face_vertex_count >= face_indices.size()) {
            throw std::runtime_error("Invalid garment OBJ face. Only triangle faces are supported.");
        }

        face_indices[face_vertex_count] = parse_face_token(face_token, vertex_count);
        ++face_vertex_count;
    }

    if (face_vertex_count != face_indices.size()) {
        throw std::runtime_error("Invalid garment OBJ face. Only triangle faces are supported.");
    }

    garment_mesh.triangle_vertex_indices.push_back(face_indices[0]);
    garment_mesh.triangle_vertex_indices.push_back(face_indices[1]);
    garment_mesh.triangle_vertex_indices.push_back(face_indices[2]);
}

void read_obj_mesh_lines(std::istream& input,
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
            parse_vertex_line(line_stream, garment_mesh, min_bounds, max_bounds, vertex_count);
        } else if (tag == "f") {
            parse_face_line(line_stream, vertex_count, garment_mesh);
        }
    }
}

void build_garment_simulation_data(GarmentMesh& garment_mesh)
{
    garment_mesh.stretch_constraints =
        build_stretch_constraints(garment_mesh.triangle_vertex_indices, garment_mesh.vertices);
    if (!garment_mesh.stretch_constraints.is_valid()) {
        throw std::runtime_error("Invalid garment stretch constraints.");
    }

    garment_mesh.bending_constraints =
        build_bending_constraints(garment_mesh.triangle_vertex_indices, garment_mesh.vertices);
    if (!garment_mesh.bending_constraints.is_valid()) {
        throw std::runtime_error("Invalid garment bending constraints.");
    }

    if (garment_mesh.garment_category == GarmentCategory::Bottom) {
        garment_mesh.attachment_vertex_indices = build_waistband_attachment_vertex_indices(garment_mesh);
        if (garment_mesh.attachment_vertex_indices.empty()) {
            throw std::runtime_error("Cannot build waistband attachment vertices.");
        }
    }
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

GarmentMesh read_garment_obj(const std::filesystem::path& obj_path, GarmentCategory garment_category)
{
    std::ifstream input(obj_path);
    if (!input) {
        throw std::runtime_error("Failed to open garment OBJ: " + obj_path.string());
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

    read_obj_mesh_lines(input, next_mesh, min_bounds, max_bounds, vertex_count);

    if (next_mesh.vertices.empty() || next_mesh.triangle_vertex_indices.empty()) {
        throw std::runtime_error("Empty garment OBJ mesh.");
    }

    assign_bounds(next_mesh, min_bounds, max_bounds);
    validate_garment_obj(next_mesh, vertex_count);

    const std::uint32_t flipped_triangle_count =
        orient_triangle_winding_outward(vertex_count,
                                        next_mesh.vertices,
                                        next_mesh.bounds_center,
                                        next_mesh.triangle_vertex_indices);
    if (flipped_triangle_count > 0u) {
        std::cout << "Oriented garment triangle winding: flipped " << flipped_triangle_count
                  << " triangles.\n";
    }

    build_garment_simulation_data(next_mesh);

    print_garment_obj_summary(obj_path, next_mesh);
    return next_mesh;
}

void write_garment_asset(const std::filesystem::path& garment_asset_path, const GarmentMesh& garment_mesh)
{
    asset_io::write_garment_asset(garment_asset_path, garment_mesh);
    std::cout << "Wrote garment asset: " << garment_asset_path << '\n';
}
