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
constexpr std::array<char, 7> garment_asset_signature = {'N', 'A', 'M', 'H', 'A', 'U', 'K'};

struct GarmentAssetCounts final {
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
    std::uint32_t adjacency_offset_count = 0;
    std::uint32_t adjacency_face_index_count = 0;
    std::uint32_t stretch_edge_count = 0;
    std::uint32_t stretch_range_count = 0;
    std::uint32_t bending_edge_count = 0;
    std::uint32_t bending_range_count = 0;
};

bool is_valid_header(const GarmentAssetCounts& counts, const GarmentMesh& garment_mesh);

GarmentAssetCounts make_garment_asset_counts(const GarmentMesh& garment_mesh)
{
    return {
        static_cast<std::uint32_t>(garment_mesh.vertices.size() / asset_io::position_components),
        static_cast<std::uint32_t>(garment_mesh.indices.size()),
        static_cast<std::uint32_t>(garment_mesh.adjacency.offsets.size()),
        static_cast<std::uint32_t>(garment_mesh.adjacency.face_indices.size()),
        static_cast<std::uint32_t>(garment_mesh.stretch_constraints.colorized_edges.size()),
        static_cast<std::uint32_t>(garment_mesh.stretch_constraints.color_ranges.size()),
        static_cast<std::uint32_t>(garment_mesh.bending_constraints.colorized_edges.size()),
        static_cast<std::uint32_t>(garment_mesh.bending_constraints.color_ranges.size())
    };
}

template <typename T>
bool read_binary_value(std::ifstream& input, T& value)
{
    input.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(input);
}

template <typename T>
bool read_binary_values(std::ifstream& input, std::vector<T>& values, std::uint32_t count)
{
    values.resize(count);
    if (values.empty()) {
        return true;
    }

    input.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(T)));
    return static_cast<bool>(input);
}

template <typename T>
bool write_binary_value(std::ofstream& output, const T& value)
{
    output.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return static_cast<bool>(output);
}

template <typename T>
bool write_binary_values(std::ofstream& output, const std::vector<T>& values)
{
    if (values.empty()) {
        return true;
    }

    output.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(T)));
    return static_cast<bool>(output);
}

bool validate_motion_asset(std::ifstream& input, const std::filesystem::path& motion_asset_path)
{
    if (!input) {
        std::cerr << "Failed to open motion asset: " << motion_asset_path << '\n';
        return false;
    }

    std::array<char, 8> magic = {};
    input.read(magic.data(), magic.size());
    if (!input || std::string(magic.data(), magic.size()) != "SMPLCACH") {
        std::cerr << "Invalid motion asset magic: " << motion_asset_path << '\n';
        return false;
    }

    return true;
}

bool is_path_inside(const std::filesystem::path& path, const std::filesystem::path& root)
{
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(path, root, error);
    if (error || relative.empty()) {
        return false;
    }

    for (const auto& part : relative) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

bool read_edges(std::ifstream& input, std::vector<MeshEdge>& edges, std::uint32_t count)
{
    edges.resize(count);
    for (MeshEdge& edge : edges) {
        if (!read_binary_value(input, edge.vertex_a) || !read_binary_value(input, edge.vertex_b)) {
            return false;
        }
    }
    return true;
}

bool read_ranges(std::ifstream& input, std::vector<MeshEdgeRange>& ranges, std::uint32_t count)
{
    ranges.resize(count);
    for (MeshEdgeRange& range : ranges) {
        if (!read_binary_value(input, range.offset) || !read_binary_value(input, range.count)) {
            return false;
        }
    }
    return true;
}

bool read_header_values(std::ifstream& input, GarmentAssetCounts& counts, GarmentMesh& garment_mesh)
{
    return read_binary_value(input, counts.vertex_count) &&
           read_binary_value(input, counts.index_count) &&
           read_binary_value(input, counts.adjacency_offset_count) &&
           read_binary_value(input, counts.adjacency_face_index_count) &&
           read_binary_value(input, counts.stretch_edge_count) &&
           read_binary_value(input, counts.stretch_range_count) &&
           read_binary_value(input, counts.bending_edge_count) &&
           read_binary_value(input, counts.bending_range_count) &&
           read_binary_value(input, garment_mesh.bounds_center.x) &&
           read_binary_value(input, garment_mesh.bounds_center.y) &&
           read_binary_value(input, garment_mesh.bounds_center.z) &&
           read_binary_value(input, garment_mesh.bounds_radius) &&
           read_binary_value(input, garment_mesh.color.x) &&
           read_binary_value(input, garment_mesh.color.y) &&
           read_binary_value(input, garment_mesh.color.z);
}

bool read_asset_header(std::ifstream& input,
                       const std::filesystem::path& garment_asset_path,
                       GarmentAssetCounts& counts,
                       GarmentMesh& garment_mesh)
{
    if (!input) {
        std::cerr << "Failed to open garment asset: " << garment_asset_path << '\n';
        return false;
    }

    std::array<char, garment_asset_signature.size()> asset_signature{};
    input.read(asset_signature.data(), static_cast<std::streamsize>(asset_signature.size()));
    if (!input || asset_signature != garment_asset_signature) {
        std::cerr << "Invalid garment asset signature: " << garment_asset_path << '\n';
        return false;
    }

    if (!read_header_values(input, counts, garment_mesh)) {
        std::cerr << "Invalid garment asset header: " << garment_asset_path << '\n';
        return false;
    }

    if (!is_valid_header(counts, garment_mesh)) {
        std::cerr << "Unsupported garment asset header values: " << garment_asset_path << '\n';
        return false;
    }

    return true;
}

bool read_mesh_data(std::ifstream& input, const GarmentAssetCounts& counts, GarmentMesh& garment_mesh)
{
    garment_mesh.adjacency.face_count = counts.index_count / 3u;
    const std::uint32_t position_value_count = counts.vertex_count * asset_io::position_components;
    return read_binary_values(input, garment_mesh.vertices, position_value_count) &&
           read_binary_values(input, garment_mesh.indices, counts.index_count) &&
           read_binary_values(input, garment_mesh.adjacency.offsets, counts.adjacency_offset_count) &&
           read_binary_values(input, garment_mesh.adjacency.face_indices, counts.adjacency_face_index_count) &&
           read_edges(input, garment_mesh.stretch_constraints.colorized_edges, counts.stretch_edge_count) &&
           read_ranges(input, garment_mesh.stretch_constraints.color_ranges, counts.stretch_range_count) &&
           read_binary_values(input, garment_mesh.stretch_constraints.rest_lengths, counts.stretch_edge_count) &&
           read_edges(input, garment_mesh.bending_constraints.colorized_edges, counts.bending_edge_count) &&
           read_ranges(input, garment_mesh.bending_constraints.color_ranges, counts.bending_range_count) &&
           read_binary_values(input, garment_mesh.bending_constraints.rest_lengths, counts.bending_edge_count);
}

bool write_edges(std::ofstream& output, const std::vector<MeshEdge>& edges)
{
    for (const MeshEdge& edge : edges) {
        if (!write_binary_value(output, edge.vertex_a) || !write_binary_value(output, edge.vertex_b)) {
            return false;
        }
    }
    return true;
}

bool write_ranges(std::ofstream& output, const std::vector<MeshEdgeRange>& ranges)
{
    for (const MeshEdgeRange& range : ranges) {
        if (!write_binary_value(output, range.offset) || !write_binary_value(output, range.count)) {
            return false;
        }
    }
    return true;
}

bool write_header_values(std::ofstream& output,
                         const GarmentAssetCounts& counts,
                         const GarmentMesh& garment_mesh)
{
    return write_binary_value(output, counts.vertex_count) &&
           write_binary_value(output, counts.index_count) &&
           write_binary_value(output, counts.adjacency_offset_count) &&
           write_binary_value(output, counts.adjacency_face_index_count) &&
           write_binary_value(output, counts.stretch_edge_count) &&
           write_binary_value(output, counts.stretch_range_count) &&
           write_binary_value(output, counts.bending_edge_count) &&
           write_binary_value(output, counts.bending_range_count) &&
           write_binary_value(output, garment_mesh.bounds_center.x) &&
           write_binary_value(output, garment_mesh.bounds_center.y) &&
           write_binary_value(output, garment_mesh.bounds_center.z) &&
           write_binary_value(output, garment_mesh.bounds_radius) &&
           write_binary_value(output, garment_mesh.color.x) &&
           write_binary_value(output, garment_mesh.color.y) &&
           write_binary_value(output, garment_mesh.color.z);
}

bool write_signature(std::ofstream& output)
{
    output.write(garment_asset_signature.data(), static_cast<std::streamsize>(garment_asset_signature.size()));
    return static_cast<bool>(output);
}

bool write_mesh_data(std::ofstream& output, const GarmentMesh& garment_mesh)
{
    return write_binary_values(output, garment_mesh.vertices) &&
           write_binary_values(output, garment_mesh.indices) &&
           write_binary_values(output, garment_mesh.adjacency.offsets) &&
           write_binary_values(output, garment_mesh.adjacency.face_indices) &&
           write_edges(output, garment_mesh.stretch_constraints.colorized_edges) &&
           write_ranges(output, garment_mesh.stretch_constraints.color_ranges) &&
           write_binary_values(output, garment_mesh.stretch_constraints.rest_lengths) &&
           write_edges(output, garment_mesh.bending_constraints.colorized_edges) &&
           write_ranges(output, garment_mesh.bending_constraints.color_ranges) &&
           write_binary_values(output, garment_mesh.bending_constraints.rest_lengths);
}

bool is_valid_edge_ranges(const std::vector<MeshEdgeRange>& ranges, std::size_t edge_count)
{
    for (const MeshEdgeRange& range : ranges) {
        const std::size_t offset = range.offset;
        const std::size_t count = range.count;
        if (count == 0u || offset > edge_count || count > edge_count - offset) {
            return false;
        }
    }
    return true;
}

bool is_valid_edges(const std::vector<MeshEdge>& edges, std::uint32_t vertex_count)
{
    for (const MeshEdge& edge : edges) {
        if (edge.vertex_a >= vertex_count || edge.vertex_b >= vertex_count || edge.vertex_a == edge.vertex_b) {
            return false;
        }
    }
    return true;
}

bool is_valid_indices(const std::vector<std::uint32_t>& indices, std::uint32_t vertex_count)
{
    for (std::uint32_t index : indices) {
        if (index >= vertex_count) {
            return false;
        }
    }
    return true;
}

bool is_valid_header(const GarmentAssetCounts& counts, const GarmentMesh& garment_mesh)
{
    return counts.vertex_count > 0u &&
           counts.index_count > 0u &&
           counts.index_count % 3u == 0u &&
           counts.adjacency_offset_count == counts.vertex_count + 1u &&
           counts.adjacency_face_index_count > 0u &&
           counts.stretch_edge_count > 0u &&
           counts.stretch_range_count > 0u &&
           counts.bending_edge_count > 0u &&
           counts.bending_range_count > 0u &&
           is_finite_vec3(garment_mesh.bounds_center) &&
           std::isfinite(garment_mesh.bounds_radius) &&
           garment_mesh.bounds_radius > 0.0f &&
           is_finite_vec3(garment_mesh.color);
}
}

namespace asset_io {
bool is_valid_garment_mesh(const GarmentMesh& garment_mesh)
{
    const GarmentAssetCounts counts = make_garment_asset_counts(garment_mesh);
    return !garment_mesh.vertices.empty() &&
           garment_mesh.vertices.size() % position_components == 0u &&
           !garment_mesh.indices.empty() &&
           garment_mesh.indices.size() % 3u == 0u &&
           is_finite_values(garment_mesh.vertices) &&
           is_valid_indices(garment_mesh.indices, counts.vertex_count) &&
           garment_mesh.adjacency.is_valid(counts.vertex_count) &&
           garment_mesh.stretch_constraints.is_valid() &&
           garment_mesh.bending_constraints.is_valid() &&
           is_valid_edges(garment_mesh.stretch_constraints.colorized_edges, counts.vertex_count) &&
           is_valid_edges(garment_mesh.bending_constraints.colorized_edges, counts.vertex_count) &&
           is_valid_edge_ranges(garment_mesh.stretch_constraints.color_ranges,
                                garment_mesh.stretch_constraints.colorized_edges.size()) &&
           is_valid_edge_ranges(garment_mesh.bending_constraints.color_ranges,
                                garment_mesh.bending_constraints.colorized_edges.size()) &&
           is_finite_values(garment_mesh.stretch_constraints.rest_lengths) &&
           is_finite_values(garment_mesh.bending_constraints.rest_lengths) &&
           is_finite_vec3(garment_mesh.bounds_center) &&
           std::isfinite(garment_mesh.bounds_radius) &&
           garment_mesh.bounds_radius > 0.0f &&
           is_finite_vec3(garment_mesh.color);
}

bool read_character_mesh_asset(const std::filesystem::path& motion_asset_path, CharacterMesh& character_mesh)
{
    std::ifstream input(motion_asset_path, std::ios::binary);
    if (!validate_motion_asset(input, motion_asset_path)) {
        return false;
    }

    std::uint32_t version = 0;
    if (!read_binary_value(input, version) ||
        !read_binary_value(input, character_mesh.fps) ||
        !read_binary_value(input, character_mesh.frame_count) ||
        !read_binary_value(input, character_mesh.vertex_count) ||
        !read_binary_value(input, character_mesh.index_count) ||
        !read_binary_value(input, character_mesh.bounds_center.x) ||
        !read_binary_value(input, character_mesh.bounds_center.y) ||
        !read_binary_value(input, character_mesh.bounds_center.z) ||
        !read_binary_value(input, character_mesh.bounds_radius)) {
        std::cerr << "Invalid motion asset header: " << motion_asset_path << '\n';
        return false;
    }

    if (version != 1 || character_mesh.fps <= 0.0f ||
        character_mesh.frame_count == 0 || character_mesh.vertex_count == 0 ||
        character_mesh.index_count == 0 || character_mesh.bounds_radius <= 0.0f) {
        std::cerr << "Unsupported motion asset header values: " << motion_asset_path << '\n';
        return false;
    }

    character_mesh.indices.resize(character_mesh.index_count);
    character_mesh.vertices.resize(
        static_cast<std::size_t>(character_mesh.frame_count) *
        static_cast<std::size_t>(character_mesh.vertex_count) * position_components
    );

    input.read(
        reinterpret_cast<char*>(character_mesh.indices.data()),
        static_cast<std::streamsize>(character_mesh.indices.size() * sizeof(std::uint32_t))
    );
    input.read(
        reinterpret_cast<char*>(character_mesh.vertices.data()),
        static_cast<std::streamsize>(character_mesh.vertices.size() * sizeof(float))
    );

    if (!input) {
        std::cerr << "Failed to read full motion asset payload: " << motion_asset_path << '\n';
        return false;
    }

    std::cout << "Loaded motion asset: " << motion_asset_path << '\n';
    std::cout << "  fps=" << character_mesh.fps
              << " frames=" << character_mesh.frame_count
              << " vertices=" << character_mesh.vertex_count
              << " indices=" << character_mesh.index_count << '\n';
    return true;
}

std::vector<std::filesystem::path> scan_motion_asset_paths(const ProjectPaths& project_paths)
{
    std::vector<std::filesystem::path> asset_paths;
    if (!std::filesystem::exists(project_paths.motion_asset_dir)) {
        return asset_paths;
    }

    for (const auto& file : std::filesystem::recursive_directory_iterator(project_paths.motion_asset_dir)) {
        if (!file.is_regular_file() || file.path().extension() != ".cache") {
            continue;
        }

        asset_paths.push_back(file.path());
    }

    std::sort(asset_paths.begin(), asset_paths.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.stem().string() < rhs.stem().string();
    });
    return asset_paths;
}

std::filesystem::path make_motion_asset_path(const ProjectPaths& project_paths, const std::filesystem::path& amass_motion_path)
{
    const std::string motion_asset_file_name = amass_motion_path.stem().string() + ".cache";

    if (is_path_inside(amass_motion_path, project_paths.amass_dir)) {
        std::error_code error;
        std::filesystem::path relative_path = std::filesystem::relative(amass_motion_path, project_paths.amass_dir, error);
        if (!error) {
            relative_path.replace_filename(motion_asset_file_name);
            return project_paths.motion_asset_dir / relative_path;
        }
    }

    return project_paths.motion_asset_dir / "imported" / motion_asset_file_name;
}

bool read_garment_mesh(const std::filesystem::path& garment_asset_path, GarmentMesh& garment_mesh)
{
    std::ifstream input(garment_asset_path, std::ios::binary);

    GarmentMesh asset_mesh;
    GarmentAssetCounts counts;

    if (!read_asset_header(input, garment_asset_path, counts, asset_mesh)) {
        return false;
    }

    if (!read_mesh_data(input, counts, asset_mesh)) {
        std::cerr << "Failed to read full garment asset payload: " << garment_asset_path << '\n';
        return false;
    }

    if (!is_valid_garment_mesh(asset_mesh)) {
        std::cerr << "Invalid garment asset payload: " << garment_asset_path << '\n';
        return false;
    }

    std::cout << "Loaded garment asset: " << garment_asset_path << '\n';
    std::cout << "  vertices=" << asset_mesh.vertices.size() / position_components
              << " triangles=" << asset_mesh.indices.size() / 3u
              << " stretch_constraints=" << asset_mesh.stretch_constraints.colorized_edges.size()
              << " stretch_color_groups=" << asset_mesh.stretch_constraints.color_ranges.size()
              << " bending_constraints=" << asset_mesh.bending_constraints.colorized_edges.size()
              << " bending_color_groups=" << asset_mesh.bending_constraints.color_ranges.size()
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
    if (!write_signature(output) ||
        !write_header_values(output, counts, garment_mesh) ||
        !write_mesh_data(output, garment_mesh)) {
        std::cerr << "Failed to write full garment asset: " << garment_asset_path << '\n';
        return false;
    }

    return true;
}

std::vector<std::filesystem::path> scan_garment_asset_paths(const ProjectPaths& project_paths)
{
    std::vector<std::filesystem::path> asset_paths;
    if (!std::filesystem::exists(project_paths.garment_asset_dir)) {
        return asset_paths;
    }

    for (const auto& file : std::filesystem::recursive_directory_iterator(project_paths.garment_asset_dir)) {
        const auto garment_asset_path = file.path();

        if (!file.is_regular_file() || garment_asset_path.extension() != ".garment") {
            continue;
        }

        asset_paths.push_back(garment_asset_path);
    }

    std::sort(asset_paths.begin(), asset_paths.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.stem().string() < rhs.stem().string();
    });
    return asset_paths;
}

std::filesystem::path make_garment_asset_path(const ProjectPaths& project_paths,
                                              const std::filesystem::path& garment_obj_path)
{
    return project_paths.garment_asset_dir / (garment_obj_path.stem().string() + ".garment");
}
}
