#include "asset/AssetIO.h"

#include "utils/NumericUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr std::array<char, 8> motion_asset_signature_v1 = {'S', 'M', 'P', 'L', 'M', 'O', 'T', 'N'};
constexpr std::array<char, 8> motion_asset_signature_v2 = {'S', 'M', 'P', 'L', 'M', 'O', 'T', '2'};
constexpr std::uint8_t max_body_part_label = 7u;
constexpr std::uint32_t quaternion_components = 4u;

struct GarmentAssetCounts final
{
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t adjacency_offset_count = 0;
    std::uint32_t adjacency_face_index_count = 0;
    std::uint32_t stretch_edge_count = 0;
    std::uint32_t stretch_range_count = 0;
    std::uint32_t bending_edge_count = 0;
    std::uint32_t bending_range_count = 0;
    std::uint32_t attachment_vertex_count = 0;
};

GarmentAssetCounts make_garment_asset_counts(const GarmentMesh& garment_mesh)
{
    return {static_cast<std::uint32_t>(garment_mesh.vertices.size() / vertex_position_components),
            static_cast<std::uint32_t>(garment_mesh.triangle_vertex_indices.size() / 3u),
            static_cast<std::uint32_t>(garment_mesh.adjacency.offsets.size()),
            static_cast<std::uint32_t>(garment_mesh.adjacency.face_indices.size()),
            static_cast<std::uint32_t>(garment_mesh.stretch_constraints.colorized_edges.size()),
            static_cast<std::uint32_t>(garment_mesh.stretch_constraints.color_ranges.size()),
            static_cast<std::uint32_t>(garment_mesh.bending_constraints.colorized_edges.size()),
            static_cast<std::uint32_t>(garment_mesh.bending_constraints.color_ranges.size()),
            static_cast<std::uint32_t>(garment_mesh.attachment_vertex_indices.size())};
}

template <typename T>
bool read_binary_value(std::ifstream& input, T& value)
{
    return static_cast<bool>(input.read(reinterpret_cast<char*>(&value), sizeof(T)));
}

template <typename T>
bool read_binary_values(std::ifstream& input, std::vector<T>& values, std::size_t count)
{
    values.resize(count);
    return values.empty() ||
           static_cast<bool>(input.read(reinterpret_cast<char*>(values.data()),
                                        static_cast<std::streamsize>(values.size() * sizeof(T))));
}

bool read_garment_asset_header_values(std::ifstream& input,
                                      GarmentAssetCounts& counts,
                                      GarmentMesh& garment_mesh)
{
    const bool counts_read = read_binary_value(input, counts.vertex_count) &&
                             read_binary_value(input, counts.triangle_count) &&
                             read_binary_value(input, counts.adjacency_offset_count) &&
                             read_binary_value(input, counts.adjacency_face_index_count) &&
                             read_binary_value(input, counts.stretch_edge_count) &&
                             read_binary_value(input, counts.stretch_range_count) &&
                             read_binary_value(input, counts.bending_edge_count) &&
                             read_binary_value(input, counts.bending_range_count) &&
                             read_binary_value(input, counts.attachment_vertex_count);
    if (!counts_read || !read_binary_value(input, garment_mesh.garment_category)) {
        return false;
    }

    return read_binary_value(input, garment_mesh.bounds_center.x) &&
           read_binary_value(input, garment_mesh.bounds_center.y) &&
           read_binary_value(input, garment_mesh.bounds_center.z) &&
           read_binary_value(input, garment_mesh.bounds_radius);
}

bool read_garment_asset_header(std::ifstream& input,
                               const std::filesystem::path& path,
                               GarmentAssetCounts& counts,
                               GarmentMesh& mesh)
{
    if (!read_garment_asset_header_values(input, counts, mesh) ||
        counts.adjacency_offset_count != counts.vertex_count + 1u) {
        std::cerr << "Failed to read garment asset header: " << path << '\n';
        return false;
    }

    return true;
}

bool read_garment_asset_data(std::ifstream& input,
                             const std::filesystem::path& path,
                             const GarmentAssetCounts& counts,
                             GarmentMesh& garment_mesh)
{
    garment_mesh.adjacency.face_count = counts.triangle_count;
    const std::uint32_t position_value_count = counts.vertex_count * vertex_position_components;
    const std::size_t triangle_vertex_index_count = static_cast<std::size_t>(counts.triangle_count) * 3u;
    if (!read_binary_values(input, garment_mesh.vertices, position_value_count) ||
        !read_binary_values(input, garment_mesh.triangle_vertex_indices, triangle_vertex_index_count) ||
        !read_binary_values(input, garment_mesh.adjacency.offsets, counts.adjacency_offset_count) ||
        !read_binary_values(input, garment_mesh.adjacency.face_indices, counts.adjacency_face_index_count) ||
        !read_binary_values(input,
                            garment_mesh.stretch_constraints.colorized_edges,
                            counts.stretch_edge_count) ||
        !read_binary_values(input,
                            garment_mesh.stretch_constraints.color_ranges,
                            counts.stretch_range_count) ||
        !read_binary_values(input,
                            garment_mesh.stretch_constraints.rest_lengths,
                            counts.stretch_edge_count) ||
        !read_binary_values(input,
                            garment_mesh.bending_constraints.colorized_edges,
                            counts.bending_edge_count) ||
        !read_binary_values(input,
                            garment_mesh.bending_constraints.color_ranges,
                            counts.bending_range_count) ||
        !read_binary_values(input,
                            garment_mesh.bending_constraints.rest_lengths,
                            counts.bending_edge_count) ||
        !read_binary_values(input, garment_mesh.attachment_vertex_indices, counts.attachment_vertex_count)) {
        std::cerr << "Failed to read full garment asset payload: " << path << '\n';
        return false;
    }

    return true;
}

bool read_motion_asset_header(std::ifstream& input,
                              const std::filesystem::path& path,
                              CharacterMesh& mesh,
                              bool& has_reference_transforms)
{
    std::array<char, motion_asset_signature_v2.size()> signature{};
    if (!input.read(signature.data(), static_cast<std::streamsize>(signature.size()))) {
        std::cerr << "Failed to read motion asset signature: " << path << '\n';
        return false;
    }

    const bool is_v1 = signature == motion_asset_signature_v1;
    const bool is_v2 = signature == motion_asset_signature_v2;
    has_reference_transforms = is_v2;
    if ((!is_v1 && !is_v2) ||
        !read_binary_value(input, mesh.fps) ||
        !read_binary_value(input, mesh.frame_count) ||
        !read_binary_value(input, mesh.vertex_count) ||
        !read_binary_value(input, mesh.triangle_count) ||
        mesh.fps <= 0.0f ||
        mesh.frame_count == 0 ||
        mesh.vertex_count == 0 ||
        mesh.triangle_count == 0) {
        std::cerr << "Invalid motion asset header: " << path << '\n';
        return false;
    }

    return true;
}

bool read_motion_asset_file_sizes(const std::filesystem::path& motion_asset_path,
                                  const CharacterMesh& character_mesh,
                                  bool has_reference_transforms,
                                  bool is_default)
{
    const std::size_t reference_position_count =
        static_cast<std::size_t>(character_mesh.frame_count) * vertex_position_components;
    const std::size_t orientation_count =
        static_cast<std::size_t>(character_mesh.frame_count) * quaternion_components;
    const std::size_t vertex_position_count = reference_position_count * character_mesh.vertex_count;
    const std::uintmax_t triangle_vertex_index_count =
        static_cast<std::uintmax_t>(character_mesh.triangle_count) * 3u;

    const std::uintmax_t base_file_size =
        motion_asset_signature_v2.size() +
        sizeof(float) +
        sizeof(std::uint32_t) * 3u +
        triangle_vertex_index_count * sizeof(std::uint32_t) +
        static_cast<std::uintmax_t>(reference_position_count) * sizeof(float) +
        static_cast<std::uintmax_t>(vertex_position_count) * sizeof(float);

    std::uintmax_t expected_file_size = base_file_size;
    if (has_reference_transforms) {
        expected_file_size += static_cast<std::uintmax_t>(orientation_count) * sizeof(float) * 2u;
        expected_file_size += static_cast<std::uintmax_t>(reference_position_count) * sizeof(float);
    }
    if (is_default) {
        expected_file_size += sizeof(std::uint32_t);
        expected_file_size += static_cast<std::uintmax_t>(character_mesh.triangle_count);
    }

    std::error_code file_size_error;
    const std::uintmax_t file_size = std::filesystem::file_size(motion_asset_path, file_size_error);
    if (file_size_error || file_size != expected_file_size) {
        std::cerr << "Invalid motion asset size: " << motion_asset_path << '\n';
        return false;
    }

    return true;
}

bool read_motion_asset_data(const std::filesystem::path& motion_asset_path,
                            CharacterMesh& character_mesh,
                            bool has_reference_transforms,
                            std::ifstream& input)
{
    const std::size_t reference_position_count =
        static_cast<std::size_t>(character_mesh.frame_count) * vertex_position_components;
    const std::size_t orientation_count =
        static_cast<std::size_t>(character_mesh.frame_count) * quaternion_components;
    const std::size_t vertex_position_count = reference_position_count * character_mesh.vertex_count;
    const std::size_t triangle_vertex_index_count =
        static_cast<std::size_t>(character_mesh.triangle_count) * 3u;

    if (!read_binary_values(input, character_mesh.triangle_vertex_indices, triangle_vertex_index_count) ||
        !read_binary_values(input, character_mesh.pelvis_positions, reference_position_count) ||
        (has_reference_transforms &&
         (!read_binary_values(input, character_mesh.pelvis_orientations, orientation_count) ||
          !read_binary_values(input, character_mesh.torso_positions, reference_position_count) ||
          !read_binary_values(input, character_mesh.torso_orientations, orientation_count))) ||
        !read_binary_values(input, character_mesh.vertices, vertex_position_count)) {
        std::cerr << "Failed to read motion asset payload: " << motion_asset_path << '\n';
        return false;
    }

    if (!has_reference_transforms) {
        character_mesh.pelvis_orientations.assign(orientation_count, 0.0f);
        character_mesh.torso_positions = character_mesh.pelvis_positions;
        character_mesh.torso_orientations.assign(orientation_count, 0.0f);
        for (std::size_t index = 3u; index < orientation_count; index += quaternion_components) {
            character_mesh.pelvis_orientations[index] = 1.0f;
            character_mesh.torso_orientations[index] = 1.0f;
        }
    }

    if (!is_finite_values(character_mesh.pelvis_positions) ||
        !is_finite_values(character_mesh.pelvis_orientations) ||
        !is_finite_values(character_mesh.torso_positions) ||
        !is_finite_values(character_mesh.torso_orientations) ||
        !is_finite_values(character_mesh.vertices)) {
        std::cerr << "Motion asset contains non-finite values: " << motion_asset_path << '\n';
        return false;
    }

    return true;
}

bool read_motion_asset_labels(const std::filesystem::path& motion_asset_path,
                              const CharacterMesh& character_mesh,
                              std::ifstream& input,
                              std::vector<std::uint8_t>& triangle_part_labels)
{
    triangle_part_labels.clear();

    const std::uint32_t triangle_count = character_mesh.triangle_count;
    std::uint32_t triangle_label_count = 0;

    const bool label_read_failed = !read_binary_value(input, triangle_label_count) ||
                                   triangle_label_count != triangle_count ||
                                   !read_binary_values(input, triangle_part_labels, triangle_label_count);
    if (label_read_failed) {
        std::cerr << "Invalid default motion asset triangle part labels: " << motion_asset_path << '\n';
        triangle_part_labels.clear();
        return false;
    }

    const bool has_invalid_label =
        std::any_of(triangle_part_labels.begin(), triangle_part_labels.end(), [](std::uint8_t label) {
            return label > max_body_part_label;
        });
    if (has_invalid_label) {
        std::cerr << "Default motion asset contains invalid triangle part labels: " << motion_asset_path
                  << '\n';
        triangle_part_labels.clear();
        return false;
    }

    return true;
}

// write //
template <typename T>
bool write_binary_value(std::ofstream& output, const T& value)
{
    return static_cast<bool>(output.write(reinterpret_cast<const char*>(&value), sizeof(T)));
}

template <typename T>
bool write_binary_values(std::ofstream& output, const std::vector<T>& values)
{
    return values.empty() ||
           static_cast<bool>(output.write(reinterpret_cast<const char*>(values.data()),
                                          static_cast<std::streamsize>(values.size() * sizeof(T))));
}

bool write_header_values(std::ofstream& output,
                         const GarmentAssetCounts& counts,
                         const GarmentMesh& garment_mesh)
{
    return write_binary_value(output, counts.vertex_count) &&
           write_binary_value(output, counts.triangle_count) &&
           write_binary_value(output, counts.adjacency_offset_count) &&
           write_binary_value(output, counts.adjacency_face_index_count) &&
           write_binary_value(output, counts.stretch_edge_count) &&
           write_binary_value(output, counts.stretch_range_count) &&
           write_binary_value(output, counts.bending_edge_count) &&
           write_binary_value(output, counts.bending_range_count) &&
           write_binary_value(output, counts.attachment_vertex_count) &&
           write_binary_value(output, garment_mesh.garment_category) &&
           write_binary_value(output, garment_mesh.bounds_center.x) &&
           write_binary_value(output, garment_mesh.bounds_center.y) &&
           write_binary_value(output, garment_mesh.bounds_center.z) &&
           write_binary_value(output, garment_mesh.bounds_radius);
}

bool write_mesh_data(std::ofstream& output, const GarmentMesh& garment_mesh)
{
    return write_binary_values(output, garment_mesh.vertices) &&
           write_binary_values(output, garment_mesh.triangle_vertex_indices) &&
           write_binary_values(output, garment_mesh.adjacency.offsets) &&
           write_binary_values(output, garment_mesh.adjacency.face_indices) &&
           write_binary_values(output, garment_mesh.stretch_constraints.colorized_edges) &&
           write_binary_values(output, garment_mesh.stretch_constraints.color_ranges) &&
           write_binary_values(output, garment_mesh.stretch_constraints.rest_lengths) &&
           write_binary_values(output, garment_mesh.bending_constraints.colorized_edges) &&
           write_binary_values(output, garment_mesh.bending_constraints.color_ranges) &&
           write_binary_values(output, garment_mesh.bending_constraints.rest_lengths) &&
           write_binary_values(output, garment_mesh.attachment_vertex_indices);
}

// validation //
bool is_valid_element_ranges(const std::vector<MeshElementRange>& ranges, std::size_t element_count)
{
    for (const auto& range : ranges) {
        const std::size_t range_end = static_cast<std::size_t>(range.offset) + range.count;
        if (range.count == 0u || range_end > element_count) {
            return false;
        }
    }
    return true;
}

bool is_valid_edges(const std::vector<MeshEdge>& edges, std::uint32_t vertex_count)
{
    for (const auto& edge : edges) {
        const bool in_range = edge.vertex_a < vertex_count && edge.vertex_b < vertex_count;
        const bool different_vertices = edge.vertex_a != edge.vertex_b;
        if (!in_range || !different_vertices) {
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

bool is_valid_distance_constraints(const GarmentDistanceConstraints& constraints, std::uint32_t vertex_count)
{
    return constraints.is_valid() &&
           is_valid_edges(constraints.colorized_edges, vertex_count) &&
           is_valid_element_ranges(constraints.color_ranges, constraints.colorized_edges.size()) &&
           is_finite_values(constraints.rest_lengths);
}

bool is_valid_garment_mesh(const GarmentMesh& garment_mesh)
{
    const bool has_valid_garment_category = garment_mesh.garment_category == GarmentCategory::Top ||
                                            garment_mesh.garment_category == GarmentCategory::Bottom ||
                                            garment_mesh.garment_category == GarmentCategory::FullBody;
    if (!has_valid_garment_category ||
        garment_mesh.vertices.empty() ||
        garment_mesh.vertices.size() % vertex_position_components != 0u ||
        !is_finite_values(garment_mesh.vertices)) {
        return false;
    }

    const auto vertex_count =
        static_cast<std::uint32_t>(garment_mesh.vertices.size() / vertex_position_components);

    if (garment_mesh.triangle_vertex_indices.empty() ||
        garment_mesh.triangle_vertex_indices.size() % 3u != 0u ||
        !is_valid_vertex_indices(garment_mesh.triangle_vertex_indices, vertex_count) ||
        !garment_mesh.adjacency.is_valid(vertex_count)) {
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
}

namespace asset_io {
bool read_character_mesh(const std::filesystem::path& motion_asset_path, CharacterMesh& character_mesh)
{
    std::ifstream input;
    input.open(motion_asset_path, std::ios::binary);
    bool has_reference_transforms = false;
    if (!read_motion_asset_header(input, motion_asset_path, character_mesh, has_reference_transforms) ||
        !read_motion_asset_file_sizes(motion_asset_path, character_mesh, has_reference_transforms, false) ||
        !read_motion_asset_data(motion_asset_path, character_mesh, has_reference_transforms, input)) {
        return false;
    }

    std::cout << "Loaded motion asset: " << motion_asset_path << '\n';
    std::cout << "  fps=" << character_mesh.fps << " frames=" << character_mesh.frame_count
              << " vertices=" << character_mesh.vertex_count << " triangles=" << character_mesh.triangle_count
              << '\n';
    return true;
}

bool read_default_character(const std::filesystem::path& motion_asset_path,
                            CharacterMesh& character_mesh,
                            std::vector<std::uint8_t>& triangle_part_labels)
{
    std::ifstream input;
    input.open(motion_asset_path, std::ios::binary);
    bool has_reference_transforms = false;
    if (!read_motion_asset_header(input, motion_asset_path, character_mesh, has_reference_transforms) ||
        !read_motion_asset_file_sizes(motion_asset_path, character_mesh, has_reference_transforms, true) ||
        !read_motion_asset_data(motion_asset_path, character_mesh, has_reference_transforms, input) ||
        !read_motion_asset_labels(motion_asset_path, character_mesh, input, triangle_part_labels)) {
        return false;
    }

    std::cout << "Loaded default motion asset: " << motion_asset_path << '\n';
    std::cout << "  fps=" << character_mesh.fps << " frames=" << character_mesh.frame_count
              << " vertices=" << character_mesh.vertex_count << " triangles=" << character_mesh.triangle_count
              << " triangle_part_labels=" << triangle_part_labels.size() << '\n';
    return true;
}

bool read_garment_mesh(const std::filesystem::path& garment_asset_path, GarmentMesh& garment_mesh)
{
    auto input = std::ifstream(garment_asset_path, std::ios::binary);

    GarmentMesh asset_mesh;
    GarmentAssetCounts counts;

    if (!read_garment_asset_header(input, garment_asset_path, counts, asset_mesh) ||
        !read_garment_asset_data(input, garment_asset_path, counts, asset_mesh) ||
        !is_valid_garment_mesh(asset_mesh)) {
        std::cerr << "Invalid garment asset payload: " << garment_asset_path << '\n';
        return false;
    }

    std::cout << "Loaded garment asset: " << garment_asset_path << '\n';
    std::cout << "  vertices=" << asset_mesh.vertices.size() / vertex_position_components
              << " triangles=" << asset_mesh.triangle_vertex_indices.size() / 3u
              << " stretch_constraints=" << asset_mesh.stretch_constraints.colorized_edges.size()
              << " stretch_color_groups=" << asset_mesh.stretch_constraints.color_ranges.size()
              << " bending_constraints=" << asset_mesh.bending_constraints.colorized_edges.size()
              << " bending_color_groups=" << asset_mesh.bending_constraints.color_ranges.size()
              << " attachment_vertices=" << asset_mesh.attachment_vertex_indices.size()
              << " bounds_radius=" << asset_mesh.bounds_radius << '\n';

    garment_mesh = std::move(asset_mesh);
    return true;
}

bool write_garment_asset(const std::filesystem::path& garment_asset_path, const GarmentMesh& garment_mesh)
{
    if (!is_valid_garment_mesh(garment_mesh)) {
        std::cerr << "Cannot write invalid garment asset mesh.\n";
        return false;
    }

    std::error_code error;
    const auto parent_path = garment_asset_path.parent_path();
    if (!parent_path.empty()) {
        std::filesystem::create_directories(parent_path, error);
        if (error) {
            std::cerr << "Failed to create garment asset directory: " << parent_path << '\n';
            return false;
        }
    }

    std::ofstream output(garment_asset_path, std::ios::binary);
    if (!output) {
        std::cerr << "Failed to open garment asset for writing: " << garment_asset_path << '\n';
        return false;
    }

    const GarmentAssetCounts counts = make_garment_asset_counts(garment_mesh);
    if (!write_header_values(output, counts, garment_mesh) || !write_mesh_data(output, garment_mesh)) {
        std::cerr << "Failed to write full garment asset: " << garment_asset_path << '\n';
        return false;
    }

    return true;
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
