#include "asset/AssetIO.h"

#include "utils/NumericUtils.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::size_t quat_components = 4u;

struct GarmentAssetCounts final
{
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t stretch_edge_count = 0;
    std::uint32_t stretch_color_state_count = 0;
    std::uint32_t bending_edge_count = 0;
    std::uint32_t bending_color_state_count = 0;
    std::uint32_t attachment_vertex_count = 0;
};

GarmentAssetCounts make_garment_asset_counts(const GarmentMesh& garment_mesh)
{
    return {static_cast<std::uint32_t>(garment_mesh.vertices.size() / position_components),
            static_cast<std::uint32_t>(garment_mesh.triangle_vertex_indices.size() / 3u),
            static_cast<std::uint32_t>(garment_mesh.stretch_constraints.colorized_edges.size()),
            static_cast<std::uint32_t>(garment_mesh.stretch_constraints.color_states.size()),
            static_cast<std::uint32_t>(garment_mesh.bending_constraints.colorized_edges.size()),
            static_cast<std::uint32_t>(garment_mesh.bending_constraints.color_states.size()),
            static_cast<std::uint32_t>(garment_mesh.attachment_vertex_indices.size())};
}

// validation

bool are_valid_constraint_color_states(const std::vector<ConstraintColorState>& color_states,
                                       std::size_t constraint_count)
{
    std::size_t expected_start_index = 0;
    for (const ConstraintColorState& color_state : color_states) {
        if (color_state.count == 0u || color_state.start_index != expected_start_index) {
            return false;
        }
        expected_start_index += color_state.count;
    }
    return expected_start_index == constraint_count;
}

bool is_valid_edges(const std::vector<MeshEdge>& edges, std::uint32_t vertex_count)
{
    for (const auto& edge : edges) {
        const bool has_valid_vertex_indices = edge.vertex_a < vertex_count && edge.vertex_b < vertex_count;
        const bool different_vertices = edge.vertex_a != edge.vertex_b;
        if (!has_valid_vertex_indices || !different_vertices) {
            return false;
        }
    }
    return true;
}

bool is_valid_vertex_indices(const std::vector<std::uint32_t>& indices, std::uint32_t vertex_count)
{
    for (std::uint32_t index : indices) {
        if (index >= vertex_count) {
            return false;
        }
    }
    return true;
}

bool has_distinct_triangle_vertices(const std::vector<std::uint32_t>& indices)
{
    for (std::size_t index = 0; index < indices.size(); index += 3u) {
        const std::uint32_t vertex_a = indices[index];
        const std::uint32_t vertex_b = indices[index + 1u];
        const std::uint32_t vertex_c = indices[index + 2u];
        if (vertex_a == vertex_b || vertex_b == vertex_c || vertex_c == vertex_a) {
            return false;
        }
    }
    return true;
}

bool is_valid_distance_constraints(const GarmentDistanceConstraints& constraints, std::uint32_t vertex_count)
{
    return constraints.is_valid() &&
           is_valid_edges(constraints.colorized_edges, vertex_count) &&
           are_valid_constraint_color_states(constraints.color_states, constraints.colorized_edges.size()) &&
           is_finite_values(constraints.rest_lengths);
}

bool is_valid_garment_mesh(const GarmentMesh& garment_mesh)
{
    const bool has_valid_garment_category = garment_mesh.garment_category == GarmentCategory::Top ||
                                            garment_mesh.garment_category == GarmentCategory::Bottom ||
                                            garment_mesh.garment_category == GarmentCategory::FullBody;
    if (!has_valid_garment_category ||
        garment_mesh.vertices.empty() ||
        garment_mesh.vertices.size() % position_components != 0u ||
        !is_finite_values(garment_mesh.vertices)) {
        return false;
    }

    const auto vertex_count = static_cast<std::uint32_t>(garment_mesh.vertices.size() / position_components);

    if (garment_mesh.triangle_vertex_indices.empty() ||
        garment_mesh.triangle_vertex_indices.size() % 3u != 0u ||
        !is_valid_vertex_indices(garment_mesh.triangle_vertex_indices, vertex_count) ||
        !has_distinct_triangle_vertices(garment_mesh.triangle_vertex_indices)) {
        return false;
    }

    if (!is_valid_distance_constraints(garment_mesh.stretch_constraints, vertex_count) ||
        !is_valid_distance_constraints(garment_mesh.bending_constraints, vertex_count) ||
        !is_valid_vertex_indices(garment_mesh.attachment_vertex_indices, vertex_count)) {
        return false;
    }

    return is_finite_vec3(garment_mesh.bounds_center) &&
           std::isfinite(garment_mesh.bounds_radius) &&
           garment_mesh.bounds_radius > 0.0f &&
           is_finite_vec3(garment_mesh.color);
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

    input.read(reinterpret_cast<char*>(values.data()),
               static_cast<std::streamsize>(values.size() * sizeof(T)));
}

void read_garment_asset_header(std::ifstream& input, GarmentAssetCounts& counts, GarmentMesh& garment_mesh)
{
    read_binary_value(input, counts.vertex_count);
    read_binary_value(input, counts.triangle_count);
    read_binary_value(input, counts.stretch_edge_count);
    read_binary_value(input, counts.stretch_color_state_count);
    read_binary_value(input, counts.bending_edge_count);
    read_binary_value(input, counts.bending_color_state_count);
    read_binary_value(input, counts.attachment_vertex_count);
    read_binary_value(input, garment_mesh.garment_category);
    read_binary_value(input, garment_mesh.bounds_center.x);
    read_binary_value(input, garment_mesh.bounds_center.y);
    read_binary_value(input, garment_mesh.bounds_center.z);
    read_binary_value(input, garment_mesh.bounds_radius);
}

void read_garment_asset_data(std::ifstream& input,
                             const GarmentAssetCounts& counts,
                             GarmentMesh& garment_mesh)
{
    const std::size_t position_value_count = counts.vertex_count * position_components;
    const std::size_t triangle_index_count = static_cast<std::size_t>(counts.triangle_count) * 3u;
    read_binary_values(input, garment_mesh.vertices, position_value_count);
    read_binary_values(input, garment_mesh.triangle_vertex_indices, triangle_index_count);
    read_binary_values(input, garment_mesh.stretch_constraints.colorized_edges, counts.stretch_edge_count);
    read_binary_values(input,
                       garment_mesh.stretch_constraints.color_states,
                       counts.stretch_color_state_count);
    read_binary_values(input, garment_mesh.stretch_constraints.rest_lengths, counts.stretch_edge_count);
    read_binary_values(input, garment_mesh.bending_constraints.colorized_edges, counts.bending_edge_count);
    read_binary_values(input,
                       garment_mesh.bending_constraints.color_states,
                       counts.bending_color_state_count);
    read_binary_values(input, garment_mesh.bending_constraints.rest_lengths, counts.bending_edge_count);
    read_binary_values(input, garment_mesh.attachment_vertex_indices, counts.attachment_vertex_count);
}

void read_motion_asset_header(std::ifstream& input, CharacterMotion& motion)
{
    read_binary_value(input, motion.fps);
    read_binary_value(input, motion.frame_count);
    read_binary_value(input, motion.vertex_count);
    read_binary_value(input, motion.triangle_count);

    if (!std::isfinite(motion.fps) ||
        motion.fps <= 0.0f ||
        motion.frame_count == 0 ||
        motion.vertex_count == 0 ||
        motion.triangle_count == 0) {
        throw std::runtime_error("Invalid motion asset header");
    }
}

void validate_motion_asset_file_size(const std::filesystem::path& path, const CharacterMotion& motion)
{
    const std::uintmax_t frame_count = motion.frame_count;
    const std::uintmax_t vertex_count = motion.vertex_count;
    const std::uintmax_t triangle_count = motion.triangle_count;

    const auto header_size = sizeof(float) + sizeof(std::uint32_t) * 3u;
    const auto index_size = triangle_count * 3u * sizeof(std::uint32_t);
    const auto transform_size = frame_count * (position_components + quat_components) * 2u * sizeof(float);
    const auto vertex_size = frame_count * vertex_count * position_components * sizeof(float);
    
    const auto motion_file_size = header_size + index_size + transform_size + vertex_size;
    const auto default_file_size = motion_file_size + sizeof(std::uint32_t) + triangle_count;
    const auto asset_file_size = std::filesystem::file_size(path);

    if (asset_file_size != motion_file_size && asset_file_size != default_file_size) {
        throw std::runtime_error("Invalid motion asset size");
    }
}

void read_motion_asset_data(CharacterMotion& character_motion, std::ifstream& input)
{
    const std::size_t reference_position_count = character_motion.frame_count * position_components;
    const std::size_t orientation_count = character_motion.frame_count * quat_components;
    const std::size_t vertex_position_count = reference_position_count * character_motion.vertex_count;
    const std::size_t triangle_index_count = static_cast<std::size_t>(character_motion.triangle_count) * 3u;

    read_binary_values(input, character_motion.triangle_vertex_indices, triangle_index_count);
    read_binary_values(input, character_motion.pelvis_positions, reference_position_count);
    read_binary_values(input, character_motion.pelvis_orientations, orientation_count);
    read_binary_values(input, character_motion.torso_positions, reference_position_count);
    read_binary_values(input, character_motion.torso_orientations, orientation_count);
    read_binary_values(input, character_motion.vertices, vertex_position_count);

    if (!is_valid_vertex_indices(character_motion.triangle_vertex_indices, character_motion.vertex_count) ||
        !has_distinct_triangle_vertices(character_motion.triangle_vertex_indices)) {
        throw std::runtime_error("Motion asset contains invalid triangle indices");
    }

    if (!is_finite_values(character_motion.pelvis_positions) ||
        !is_finite_values(character_motion.pelvis_orientations) ||
        !is_finite_values(character_motion.torso_positions) ||
        !is_finite_values(character_motion.torso_orientations) ||
        !is_finite_values(character_motion.vertices)) {
        throw std::runtime_error("Motion asset contains non-finite values");
    }
}

CharacterMotion read_motion_asset(const std::filesystem::path& motion_asset_path, std::ifstream& input)
{
    CharacterMotion character_motion;
    read_motion_asset_header(input, character_motion);
    validate_motion_asset_file_size(motion_asset_path, character_motion);
    read_motion_asset_data(character_motion, input);
    return character_motion;
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

    output.write(reinterpret_cast<const char*>(values.data()),
                 static_cast<std::streamsize>(values.size() * sizeof(T)));
}

void write_header_values(std::ofstream& output,
                         const GarmentAssetCounts& counts,
                         const GarmentMesh& garment_mesh)
{
    write_binary_value(output, counts.vertex_count);
    write_binary_value(output, counts.triangle_count);
    write_binary_value(output, counts.stretch_edge_count);
    write_binary_value(output, counts.stretch_color_state_count);
    write_binary_value(output, counts.bending_edge_count);
    write_binary_value(output, counts.bending_color_state_count);
    write_binary_value(output, counts.attachment_vertex_count);
    write_binary_value(output, garment_mesh.garment_category);
    write_binary_value(output, garment_mesh.bounds_center.x);
    write_binary_value(output, garment_mesh.bounds_center.y);
    write_binary_value(output, garment_mesh.bounds_center.z);
    write_binary_value(output, garment_mesh.bounds_radius);
}

void write_mesh_data(std::ofstream& output, const GarmentMesh& garment_mesh)
{
    write_binary_values(output, garment_mesh.vertices);
    write_binary_values(output, garment_mesh.triangle_vertex_indices);
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
              << " vertices=" << character_motion.vertex_count
              << " triangles=" << character_motion.triangle_count << '\n';

    return character_motion;
}

CharacterMotion read_default_character(const std::filesystem::path& motion_asset_path,
                                       std::vector<std::uint8_t>& triangle_part_labels)
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

    return character_motion;
}

GarmentMesh read_garment_mesh(const std::filesystem::path& garment_asset_path)
{
    std::ifstream input;
    input.exceptions(std::ios::failbit | std::ios::badbit);
    input.open(garment_asset_path, std::ios::binary);

    GarmentMesh garment_mesh;
    GarmentAssetCounts counts;
    read_garment_asset_header(input, counts, garment_mesh);
    read_garment_asset_data(input, counts, garment_mesh);
    if (!is_valid_garment_mesh(garment_mesh)) {
        throw std::runtime_error("Invalid garment asset payload");
    }

    std::cout << "Loaded garment asset: " << garment_asset_path << '\n';
    std::cout << "  vertices=" << garment_mesh.vertices.size() / position_components
              << " triangles=" << garment_mesh.triangle_vertex_indices.size() / 3u
              << " stretch_constraints=" << garment_mesh.stretch_constraints.colorized_edges.size()
              << " stretch_color_states=" << garment_mesh.stretch_constraints.color_states.size()
              << " bending_constraints=" << garment_mesh.bending_constraints.colorized_edges.size()
              << " bending_color_states=" << garment_mesh.bending_constraints.color_states.size()
              << " attachment_vertices=" << garment_mesh.attachment_vertex_indices.size()
              << " bounds_radius=" << garment_mesh.bounds_radius << '\n';
    return garment_mesh;
}

void write_garment_asset(const std::filesystem::path& garment_asset_path, const GarmentMesh& garment_mesh)
{
    if (!is_valid_garment_mesh(garment_mesh)) {
        throw std::runtime_error("Cannot write invalid garment asset mesh.");
    }

    const auto parent_path = garment_asset_path.parent_path();
    if (!parent_path.empty()) {
        std::filesystem::create_directories(parent_path);
    }

    std::ofstream output;
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.open(garment_asset_path, std::ios::binary);

    const GarmentAssetCounts counts = make_garment_asset_counts(garment_mesh);
    write_header_values(output, counts, garment_mesh);
    write_mesh_data(output, garment_mesh);
    output.close();
}

std::filesystem::path make_garment_asset_path(const ProjectPaths& project_paths,
                                              const std::filesystem::path& garment_obj_path)
{
    return project_paths.garment_asset_dir / (garment_obj_path.stem().string() + ".garment");
}

// asset directory 아래 지정한 확장자의 asset file 경로를 정렬해 반환
std::vector<std::filesystem::path> scan_asset_paths(const std::filesystem::path& asset_dir,
                                                    const std::filesystem::path& asset_extension)
{
    if (!std::filesystem::is_directory(asset_dir)) {
        return {};
    }

    std::vector<std::filesystem::path> asset_paths;
    for (const auto& file : std::filesystem::recursive_directory_iterator(asset_dir)) {
        if (!file.is_regular_file() || file.path().extension() != asset_extension) {
            continue;
        }
        asset_paths.push_back(file.path());
    }

    std::sort(asset_paths.begin(), asset_paths.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.stem() < rhs.stem();
    });
    return asset_paths;
}
}
