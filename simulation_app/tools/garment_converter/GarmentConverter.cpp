#include "GarmentConverter.h"

#include "asset/AssetIO.h"
#include "utils/MeshGeometryUtils.h"
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
glm::vec3 parse_vertex_line(std::istringstream& line_stream)
{
    glm::vec3 vertex{};
    if (!(line_stream >> vertex.x >> vertex.y >> vertex.z)) {
        throw std::runtime_error("Invalid garment OBJ vertex.");
    }

    constexpr float obj_to_world_scale = 0.001f;
    vertex *= obj_to_world_scale;
    if (!is_finite_vec3(vertex)) {
        throw std::runtime_error("Invalid garment OBJ vertex.");
    }
    return vertex;
}

std::uint32_t parse_face_token(std::string_view token, std::uint32_t vertex_count)
{
    std::uint32_t parsed_index = 0;
    const auto vertex_text = token.substr(0, token.find('/'));
    const char* text_begin = vertex_text.data();
    const char* text_end = text_begin + vertex_text.size();
    const auto [parsed_end, error] = std::from_chars(text_begin, text_end, parsed_index);

    if (error != std::errc{} || parsed_end != text_end || parsed_index == 0 || parsed_index > vertex_count) {
        throw std::runtime_error("Invalid garment OBJ face. Only triangle faces are supported.");
    }

    return parsed_index - 1u;
}

void parse_face_line(std::istringstream& line_stream, GarmentMesh& mesh)
{
    std::array<std::string, 3> tokens;
    std::string extra_token;
    if (!(line_stream >> tokens[0] >> tokens[1] >> tokens[2]) || (line_stream >> extra_token)) {
        throw std::runtime_error("Garment OBJ faces must contain exactly three vertices.");
    }

    const auto vertex_count = static_cast<std::uint32_t>(mesh.vertices.size() / position_components);
    for (const std::string& face_token : tokens) {
        mesh.triangle_vertex_indices.push_back(parse_face_token(face_token, vertex_count));
    }
}

void read_obj_mesh_lines(std::istream& input, GarmentMesh& mesh)
{
    glm::vec3 min_bounds{std::numeric_limits<float>::max()};
    glm::vec3 max_bounds{std::numeric_limits<float>::lowest()};

    std::string line;
    while (std::getline(input, line)) {
        std::istringstream line_stream(line);
        std::string tag;
        if (!(line_stream >> tag) || tag.front() == '#') {
            continue;
        }

        if (tag == "v") {
            const glm::vec3 vertex = parse_vertex_line(line_stream);
            mesh.vertices.insert(mesh.vertices.end(), {vertex.x, vertex.y, vertex.z});
            min_bounds = glm::min(min_bounds, vertex);
            max_bounds = glm::max(max_bounds, vertex);
        } else if (tag == "f") {
            parse_face_line(line_stream, mesh);
        }
    }

    mesh.bounds_center = (min_bounds + max_bounds) * 0.5f;
    mesh.bounds_radius = glm::length(max_bounds - min_bounds) * 0.5f;

    if (mesh.vertices.empty() ||
        mesh.triangle_vertex_indices.empty() ||
        !std::isfinite(mesh.bounds_radius) ||
        mesh.bounds_radius <= 0.0f) {
        throw std::runtime_error("Invalid garment OBJ.");
    }
}

void validate_garment_topology(const GarmentMesh& mesh)
{
    const auto vertex_count = static_cast<std::uint32_t>(mesh.vertices.size() / position_components);
    std::vector<std::uint8_t> used_vertices(vertex_count, 0u);

    // check zero-area triangle
    for (std::size_t index = 0; index < mesh.triangle_vertex_indices.size(); index += 3u) {
        const std::uint32_t vertex_a = mesh.triangle_vertex_indices[index];
        const std::uint32_t vertex_b = mesh.triangle_vertex_indices[index + 1u];
        const std::uint32_t vertex_c = mesh.triangle_vertex_indices[index + 2u];

        const glm::vec3 position_a = get_vertex_position(mesh.vertices, vertex_a);
        const glm::vec3 position_b = get_vertex_position(mesh.vertices, vertex_b);
        const glm::vec3 position_c = get_vertex_position(mesh.vertices, vertex_c);

        if (glm::length(glm::cross(position_b - position_a, position_c - position_a)) <= 1.0e-10f) {
            throw std::runtime_error("Garment OBJ contains a zero-area triangle.");
        }

        used_vertices[vertex_a] = 1u;
        used_vertices[vertex_b] = 1u;
        used_vertices[vertex_c] = 1u;
    }

    // check unused vertex
    if (std::find(used_vertices.begin(), used_vertices.end(), 0u) != used_vertices.end()) {
        throw std::runtime_error("Garment OBJ contains an unused vertex.");
    }
}

GarmentDistanceConstraints build_distance_constraints(const std::vector<MeshEdge>& edges,
                                                      const std::vector<float>& vertices)
{
    const auto vertex_count = static_cast<std::uint32_t>(vertices.size() / position_components);
    ColorizedMeshEdges colorized_edges = colorize_mesh_edges(vertex_count, edges);

    GarmentDistanceConstraints constraints;
    constraints.colorized_edges = std::move(colorized_edges.edges);
    constraints.color_states = std::move(colorized_edges.color_states);
    constraints.rest_lengths = compute_mesh_edge_lengths(constraints.colorized_edges, vertices);

    if (!constraints.is_valid()) {
        throw std::runtime_error("Invalid garment distance constraints.");
    }
    return constraints;
}

std::vector<std::uint32_t> build_waistband_attachment_vertex_indices(const GarmentMesh& mesh)
{
    // Waistband attachment: top 3cm of the mesh
    const auto vertex_count = static_cast<std::uint32_t>(mesh.vertices.size() / position_components);

    float max_y = std::numeric_limits<float>::lowest();
    for (std::uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        max_y = std::max(max_y, get_vertex_position(mesh.vertices, vertex_index).y);
    }

    constexpr float waistband_attachment_band_height = 0.03f;
    const float attachment_min_y = max_y - waistband_attachment_band_height;

    std::vector<std::uint32_t> attachment_vertex_indices;
    for (std::uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        if (get_vertex_position(mesh.vertices, vertex_index).y >= attachment_min_y) {
            attachment_vertex_indices.push_back(vertex_index);
        }
    }
    return attachment_vertex_indices;
}

void build_garment_constraints(GarmentMesh& mesh)
{
    const auto triangle_edges = build_unique_triangle_edges(mesh.triangle_vertex_indices);
    mesh.stretch_constraints = build_distance_constraints(triangle_edges, mesh.vertices);

    const auto bending_edges = build_unique_bending_edges(mesh.triangle_vertex_indices);
    mesh.bending_constraints = build_distance_constraints(bending_edges, mesh.vertices);

    if (mesh.garment_category == GarmentCategory::Bottom) {
        mesh.attachment_vertex_indices = build_waistband_attachment_vertex_indices(mesh);
    }
}

void print_garment_obj_summary(const std::filesystem::path& obj_path, const GarmentMesh& mesh)
{
    std::cout << "Read garment OBJ: " << obj_path << '\n';
    std::cout << "  vertices=" << mesh.vertices.size() / position_components
              << " triangles=" << mesh.triangle_vertex_indices.size() / 3u
              << " stretch_constraints=" << mesh.stretch_constraints.colorized_edges.size()
              << " bending_constraints=" << mesh.bending_constraints.colorized_edges.size()
              << " attachment_vertices=" << mesh.attachment_vertex_indices.size()
              << " bounds_radius=" << mesh.bounds_radius << '\n';
}
}

void convert_garment(const std::filesystem::path& obj_path,
                     const std::filesystem::path& garment_asset_path,
                     GarmentCategory garment_category)
{
    std::ifstream input(obj_path);
    if (!input) {
        throw std::runtime_error("Failed to open garment OBJ: " + obj_path.string());
    }

    GarmentMesh mesh;
    mesh.garment_category = garment_category;
    read_obj_mesh_lines(input, mesh);
    validate_garment_topology(mesh);
    orient_triangles_outward(mesh.vertices, mesh.bounds_center, mesh.triangle_vertex_indices);
    build_garment_constraints(mesh);

    print_garment_obj_summary(obj_path, mesh);
    asset_io::write_garment_mesh(garment_asset_path, mesh);
    std::cout << "Wrote garment asset: " << garment_asset_path << '\n';
}
