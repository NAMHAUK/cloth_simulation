#include "asset/AssetIO.h"
#include "utils/NumericUtils.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

#include <glm/geometric.hpp>

namespace {
constexpr std::size_t quat_components = 4u;

struct GarmentAssetCounts final
{
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t triangle_color_count = 0;
    std::uint32_t stretch_edge_count = 0;
    std::uint32_t stretch_color_state_count = 0;
    std::uint32_t bending_edge_count = 0;
    std::uint32_t bending_color_state_count = 0;
    std::uint32_t attachment_vertex_count = 0;
};

// validation

void validate_indices(const std::vector<std::uint32_t>& indices, std::size_t count)
{
    for (std::uint32_t index : indices) {
        if (index >= count) {
            throw std::runtime_error("Asset index out of range");
        }
    }
}

void validate_orientations(const std::vector<float>& orientations)
{
    for (std::size_t index = 0; index < orientations.size(); index += quat_components) {
        const float x = orientations[index];
        const float y = orientations[index + 1u];
        const float z = orientations[index + 2u];
        const float w = orientations[index + 3u];
        const float length_squared = x * x + y * y + z * z + w * w;
        if (!std::isfinite(length_squared) || length_squared <= 0.0f) {
            throw std::runtime_error("Motion asset contains invalid orientations");
        }
    }
}

void validate_triangles(const std::vector<TriangleVertexIndices>& vertex_indices, std::size_t vertex_count)
{
    if (vertex_indices.empty()) {
        throw std::runtime_error("Asset contains no triangles");
    }

    for (const TriangleVertexIndices& triangle : vertex_indices) {
        if (triangle.a >= vertex_count ||
            triangle.b >= vertex_count ||
            triangle.c >= vertex_count ||
            triangle.a == triangle.b ||
            triangle.b == triangle.c ||
            triangle.c == triangle.a) {
            throw std::runtime_error("Invalid triangle vertex indices");
        }
    }
}

void validate_triangle_coloring(const GarmentMesh& mesh)
{
    const auto colored_triangle_count = std::accumulate(mesh.triangle_color_counts.begin(),
                                                        mesh.triangle_color_counts.end(),
                                                        std::uint64_t{0});
    if (colored_triangle_count != mesh.triangle_vertex_indices.size()) {
        throw std::runtime_error("Invalid garment triangle color counts");
    }
}

void validate_distance_constraints(const GarmentDistanceConstraints& constraints, std::size_t vertex_count)
{
    if (constraints.colorized_edges.empty() || constraints.colorized_edges.size() != constraints.rest_lengths.size()) {
        throw std::runtime_error("Invalid garment constraint counts");
    }

    for (const auto& edge : constraints.colorized_edges) {
        if (edge.vertex_a >= vertex_count || edge.vertex_b >= vertex_count || edge.vertex_a == edge.vertex_b) {
            throw std::runtime_error("Invalid edge vertex indices");
        }
    }

    for (float rest_length : constraints.rest_lengths) {
        if (!std::isfinite(rest_length) || rest_length <= 0.0f) {
            throw std::runtime_error("Invalid garment distance constraint.");
        }
    }

    // Validate color ranges.
    std::size_t expected_start_index = 0;
    for (const ConstraintColorState& color_state : constraints.color_states) {
        if (color_state.count == 0u || color_state.start_index != expected_start_index) {
            throw std::runtime_error("Invalid constraint color range");
        }
        expected_start_index += color_state.count;
    }
    if (expected_start_index != constraints.colorized_edges.size()) {
        throw std::runtime_error("Invalid constraint color count");
    }
}

void validate_motion_asset_file_size(const std::filesystem::path& path, const CharacterMotion& motion)
{
    const std::uintmax_t frame_count = motion.frame_count;
    const std::uintmax_t vertex_count = motion.vertex_count;
    const std::uintmax_t triangle_count = motion.triangle_count;

    const auto header_size = sizeof(float) + sizeof(std::uint32_t) * 3u;
    const auto index_size = triangle_count * sizeof(TriangleVertexIndices);
    const auto transform_size = frame_count * (position_components + quat_components) * 2u * sizeof(float);
    const auto non_vertex_size = header_size + index_size + transform_size;
    const auto vertex_stride = sizeof(glm::vec3);
    const auto asset_file_size = std::filesystem::file_size(path);

    if (non_vertex_size > asset_file_size ||
        (frame_count != 0u && vertex_count > (asset_file_size - non_vertex_size) / frame_count / vertex_stride)) {
        throw std::runtime_error("Invalid motion asset size");
    }
}

void validate_garment_asset_file_size(const std::filesystem::path& path, const GarmentAssetCounts& counts)
{
    const auto header_size = sizeof(std::uint32_t) * 8u + sizeof(GarmentCategory) + sizeof(float) * 4u;
    const auto edge_data_size = sizeof(MeshEdge) + sizeof(float);

    std::uintmax_t expected_file_size = header_size;
    expected_file_size += std::uintmax_t{counts.vertex_count} * sizeof(glm::vec3);
    expected_file_size += std::uintmax_t{counts.triangle_count} * sizeof(TriangleVertexIndices);
    expected_file_size += std::uintmax_t{counts.triangle_color_count} * sizeof(std::uint32_t);
    expected_file_size += std::uintmax_t{counts.stretch_edge_count} * edge_data_size;
    expected_file_size += std::uintmax_t{counts.stretch_color_state_count} * sizeof(ConstraintColorState);
    expected_file_size += std::uintmax_t{counts.bending_edge_count} * edge_data_size;
    expected_file_size += std::uintmax_t{counts.bending_color_state_count} * sizeof(ConstraintColorState);
    expected_file_size += std::uintmax_t{counts.attachment_vertex_count} * sizeof(std::uint32_t);

    if (std::filesystem::file_size(path) != expected_file_size) {
        throw std::runtime_error("Invalid garment asset size");
    }
}

void validate_character_motion(const CharacterMotion& motion)
{
    if (!std::isfinite(motion.fps) ||
        motion.fps <= 0.0f ||
        motion.frame_count == 0u ||
        motion.vertex_count == 0u ||
        motion.triangle_count == 0u) {
        throw std::runtime_error("Invalid motion asset header");
    }

    validate_triangles(motion.triangle_vertex_indices, motion.vertex_count);

    if (!is_finite_values(motion.pelvis_positions) ||
        !is_finite_values(motion.torso_positions) ||
        !std::all_of(motion.vertex_positions.begin(), motion.vertex_positions.end(), is_finite_vec3)) {
        throw std::runtime_error("Motion asset contains non-finite values");
    }

    validate_orientations(motion.pelvis_orientations);
    validate_orientations(motion.torso_orientations);
}

void validate_garment_geometry(const GarmentMesh& mesh)
{
    for (const auto& [vertex_a, vertex_b, vertex_c] : mesh.triangle_vertex_indices) {
        const glm::dvec3 position_a = mesh.vertex_positions[vertex_a];
        const glm::dvec3 position_b = mesh.vertex_positions[vertex_b];
        const glm::dvec3 position_c = mesh.vertex_positions[vertex_c];

        if (glm::cross(position_b - position_a, position_c - position_a) == glm::dvec3{0.0}) {
            throw std::runtime_error("Degenerate garment rest triangle.");
        }
    }

    // Bending constraints
    for (const auto& [vertex_a, vertex_b] : mesh.bending_constraints.colorized_edges) {
        if (mesh.vertex_positions[vertex_a] == mesh.vertex_positions[vertex_b]) {
            throw std::runtime_error("Invalid garment distance constraint.");
        }
    }
}

void validate_garment_mesh(const GarmentMesh& mesh)
{
    if (mesh.garment_category != GarmentCategory::Top &&
        mesh.garment_category != GarmentCategory::Bottom &&
        mesh.garment_category != GarmentCategory::FullBody) {
        throw std::runtime_error("Invalid garment category");
    }

    if (mesh.vertex_positions.empty() ||
        !std::all_of(mesh.vertex_positions.begin(), mesh.vertex_positions.end(), is_finite_vec3)) {
        throw std::runtime_error("Invalid garment vertices");
    }

    const auto vertex_count = mesh.vertex_positions.size();
    validate_triangles(mesh.triangle_vertex_indices, vertex_count);
    validate_triangle_coloring(mesh);
    validate_distance_constraints(mesh.stretch_constraints, vertex_count);
    validate_distance_constraints(mesh.bending_constraints, vertex_count);
    validate_indices(mesh.attachment_vertex_indices, vertex_count);
    validate_garment_geometry(mesh);

    if (!is_finite_vec3(mesh.bounds_center) || !std::isfinite(mesh.bounds_radius) || mesh.bounds_radius <= 0.0f) {
        throw std::runtime_error("Invalid garment bounds");
    }
}

// read
template <typename T>
void read_binary_value(std::ifstream& input, T& value)
{
    input.read(reinterpret_cast<char*>(&value), sizeof(T));
}

template <typename T>
void read_binary_values(std::ifstream& input, std::vector<T>& values, std::size_t count)
{
    values.resize(count);
    if (values.empty()) {
        return;
    }

    input.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(T)));
}

CharacterMotion read_motion_asset(const std::filesystem::path& motion_asset_path, std::ifstream& input)
{
    CharacterMotion character_motion;
    read_binary_value(input, character_motion.fps);
    read_binary_value(input, character_motion.frame_count);
    read_binary_value(input, character_motion.vertex_count);
    read_binary_value(input, character_motion.triangle_count);

    validate_motion_asset_file_size(motion_asset_path, character_motion);

    const std::size_t reference_position_count = character_motion.frame_count * position_components;
    const std::size_t orientation_count = character_motion.frame_count * quat_components;
    const std::size_t vertex_position_count = static_cast<std::size_t>(character_motion.frame_count) *
                                              character_motion.vertex_count;
    read_binary_values(input, character_motion.triangle_vertex_indices, character_motion.triangle_count);
    read_binary_values(input, character_motion.pelvis_positions, reference_position_count);
    read_binary_values(input, character_motion.pelvis_orientations, orientation_count);
    read_binary_values(input, character_motion.torso_positions, reference_position_count);
    read_binary_values(input, character_motion.torso_orientations, orientation_count);
    read_binary_values(input, character_motion.vertex_positions, vertex_position_count);

    validate_character_motion(character_motion);
    return character_motion;
}

GarmentMesh read_garment_asset(const std::filesystem::path& garment_asset_path, std::ifstream& input)
{
    GarmentAssetCounts counts;
    GarmentMesh mesh;
    read_binary_value(input, counts.vertex_count);
    read_binary_value(input, counts.triangle_count);
    read_binary_value(input, counts.triangle_color_count);
    read_binary_value(input, counts.stretch_edge_count);
    read_binary_value(input, counts.stretch_color_state_count);
    read_binary_value(input, counts.bending_edge_count);
    read_binary_value(input, counts.bending_color_state_count);
    read_binary_value(input, counts.attachment_vertex_count);
    read_binary_value(input, mesh.garment_category);
    read_binary_value(input, mesh.bounds_center.x);
    read_binary_value(input, mesh.bounds_center.y);
    read_binary_value(input, mesh.bounds_center.z);
    read_binary_value(input, mesh.bounds_radius);

    validate_garment_asset_file_size(garment_asset_path, counts);

    read_binary_values(input, mesh.vertex_positions, counts.vertex_count);
    read_binary_values(input, mesh.triangle_vertex_indices, counts.triangle_count);
    read_binary_values(input, mesh.triangle_color_counts, counts.triangle_color_count);
    read_binary_values(input, mesh.stretch_constraints.colorized_edges, counts.stretch_edge_count);
    read_binary_values(input, mesh.stretch_constraints.color_states, counts.stretch_color_state_count);
    read_binary_values(input, mesh.stretch_constraints.rest_lengths, counts.stretch_edge_count);
    read_binary_values(input, mesh.bending_constraints.colorized_edges, counts.bending_edge_count);
    read_binary_values(input, mesh.bending_constraints.color_states, counts.bending_color_state_count);
    read_binary_values(input, mesh.bending_constraints.rest_lengths, counts.bending_edge_count);
    read_binary_values(input, mesh.attachment_vertex_indices, counts.attachment_vertex_count);

    validate_garment_mesh(mesh);
    return mesh;
}

// write

template <typename T>
void write_binary_value(std::ofstream& output, const T& value)
{
    output.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
void write_binary_values(std::ofstream& output, const std::vector<T>& values)
{
    if (values.empty()) {
        return;
    }

    output.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(T)));
}

void write_garment_asset(std::ofstream& output, const GarmentMesh& garment_mesh)
{
    write_binary_value<std::uint32_t>(output, garment_mesh.vertex_positions.size());
    write_binary_value<std::uint32_t>(output, garment_mesh.triangle_vertex_indices.size());
    write_binary_value<std::uint32_t>(output, garment_mesh.triangle_color_counts.size());
    write_binary_value<std::uint32_t>(output, garment_mesh.stretch_constraints.colorized_edges.size());
    write_binary_value<std::uint32_t>(output, garment_mesh.stretch_constraints.color_states.size());
    write_binary_value<std::uint32_t>(output, garment_mesh.bending_constraints.colorized_edges.size());
    write_binary_value<std::uint32_t>(output, garment_mesh.bending_constraints.color_states.size());
    write_binary_value<std::uint32_t>(output, garment_mesh.attachment_vertex_indices.size());
    write_binary_value(output, garment_mesh.garment_category);
    write_binary_value(output, garment_mesh.bounds_center.x);
    write_binary_value(output, garment_mesh.bounds_center.y);
    write_binary_value(output, garment_mesh.bounds_center.z);
    write_binary_value(output, garment_mesh.bounds_radius);

    write_binary_values(output, garment_mesh.vertex_positions);
    write_binary_values(output, garment_mesh.triangle_vertex_indices);
    write_binary_values(output, garment_mesh.triangle_color_counts);
    write_binary_values(output, garment_mesh.stretch_constraints.colorized_edges);
    write_binary_values(output, garment_mesh.stretch_constraints.color_states);
    write_binary_values(output, garment_mesh.stretch_constraints.rest_lengths);
    write_binary_values(output, garment_mesh.bending_constraints.colorized_edges);
    write_binary_values(output, garment_mesh.bending_constraints.color_states);
    write_binary_values(output, garment_mesh.bending_constraints.rest_lengths);
    write_binary_values(output, garment_mesh.attachment_vertex_indices);
}
}

namespace asset_io {
CharacterMotion read_character_motion(const std::filesystem::path& motion_asset_path)
{
    std::ifstream input;
    input.exceptions(std::ios::failbit | std::ios::badbit);
    input.open(motion_asset_path, std::ios::binary);

    CharacterMotion character_motion = read_motion_asset(motion_asset_path, input);

    std::cout << "Loaded motion asset: " << motion_asset_path << '\n';
    std::cout << "  fps=" << character_motion.fps << " frames=" << character_motion.frame_count
              << " vertices=" << character_motion.vertex_count << " triangles=" << character_motion.triangle_count
              << '\n';

    return character_motion;
}

CharacterMotion read_default_character(const std::filesystem::path& motion_asset_path,
                                       std::vector<std::uint8_t>& triangle_part_labels,
                                       std::vector<std::uint32_t>& triangle_color_counts)
{
    std::ifstream input;
    input.exceptions(std::ios::failbit | std::ios::badbit);
    input.open(motion_asset_path, std::ios::binary);

    CharacterMotion character_motion = read_motion_asset(motion_asset_path, input);
    if (character_motion.frame_count != 1u) {
        throw std::runtime_error("Default character asset must contain one frame");
    }

    // read per triangle part labels
    std::uint32_t labeled_triangle_count = 0;
    read_binary_value(input, labeled_triangle_count);
    read_binary_values(input, triangle_part_labels, character_motion.triangle_count);

    if (labeled_triangle_count != character_motion.triangle_count ||
        std::any_of(triangle_part_labels.begin(),
                    triangle_part_labels.end(),
                    [](std::uint8_t label) { return label >= body_part_label_count; }) ||
        std::none_of(triangle_part_labels.begin(), triangle_part_labels.end(), [](std::uint8_t label) {
            return !is_hand_body_part_label(label);
        })) {
        throw std::runtime_error("Invalid default character triangle part labels");
    }

    // Read triangle color
    std::uint32_t color_count = 0;
    read_binary_value(input, color_count);
    if (color_count == 0u || color_count > character_motion.triangle_count) {
        throw std::runtime_error("Invalid character triangle coloring counts");
    }

    const std::uintmax_t expected_file_size = static_cast<std::streamoff>(input.tellg()) +
                                              std::uintmax_t{color_count} * sizeof(std::uint32_t);
    if (std::filesystem::file_size(motion_asset_path) != expected_file_size) {
        throw std::runtime_error("Invalid character triangle coloring resource size");
    }

    read_binary_values(input, triangle_color_counts, color_count);
    const auto colored_triangle_count = std::accumulate(triangle_color_counts.begin(),
                                                        triangle_color_counts.end(),
                                                        std::uint64_t{0});
    if (std::find(triangle_color_counts.begin(), triangle_color_counts.end(), 0u) != triangle_color_counts.end() ||
        colored_triangle_count != character_motion.triangle_count) {
        throw std::runtime_error("Invalid character triangle color counts");
    }

    return character_motion;
}

GarmentMesh read_garment_mesh(const std::filesystem::path& garment_asset_path)
{
    std::ifstream input;
    input.exceptions(std::ios::failbit | std::ios::badbit);
    input.open(garment_asset_path, std::ios::binary);

    GarmentMesh garment_mesh = read_garment_asset(garment_asset_path, input);

    std::cout << "Loaded garment asset: " << garment_asset_path << '\n';
    std::cout << "  vertices=" << garment_mesh.vertex_positions.size()
              << " triangles=" << garment_mesh.triangle_vertex_indices.size()
              << " stretch_constraints=" << garment_mesh.stretch_constraints.colorized_edges.size()
              << " stretch_color_states=" << garment_mesh.stretch_constraints.color_states.size()
              << " bending_constraints=" << garment_mesh.bending_constraints.colorized_edges.size()
              << " bending_color_states=" << garment_mesh.bending_constraints.color_states.size()
              << " attachment_vertices=" << garment_mesh.attachment_vertex_indices.size()
              << " bounds_radius=" << garment_mesh.bounds_radius << '\n';

    return garment_mesh;
}

void write_garment_mesh(const std::filesystem::path& garment_asset_path, const GarmentMesh& garment_mesh)
{
    const auto parent_path = garment_asset_path.parent_path();
    if (!parent_path.empty()) {
        std::filesystem::create_directories(parent_path);
    }

    std::ofstream output;
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.open(garment_asset_path, std::ios::binary);

    write_garment_asset(output, garment_mesh);
    output.close();
}

std::vector<std::filesystem::path> scan_asset_paths(const std::filesystem::path& asset_dir,
                                                    const std::filesystem::path& asset_extension)
{
    if (!std::filesystem::is_directory(asset_dir)) {
        return {};
    }

    std::vector<std::filesystem::path> asset_paths;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(asset_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == asset_extension) {
            asset_paths.push_back(entry.path());
        }
    }

    std::sort(asset_paths.begin(), asset_paths.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.stem() < rhs.stem();
    });
    return asset_paths;
}
}
