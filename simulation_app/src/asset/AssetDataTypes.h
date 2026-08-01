#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

inline constexpr std::uint32_t vertex_position_components = 3;

struct VertexFaceAdjacency final
{
    std::vector<std::uint32_t> offsets;
    std::vector<std::uint32_t> face_indices;
    std::uint32_t face_count = 0;

    bool is_valid(std::uint32_t vertex_count) const;
};

struct MeshEdge final
{
    std::uint32_t vertex_a = 0;
    std::uint32_t vertex_b = 0;
};

struct MeshElementRange final
{
    std::uint32_t offset = 0;
    std::uint32_t count = 0;
};

struct ColorizedMeshEdges final
{
    std::vector<MeshEdge> edges;
    std::vector<MeshElementRange> ranges;
};

struct CharacterMesh
{
    float fps = 0.0f;
    std::uint32_t frame_count = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
    std::vector<std::uint32_t> triangle_vertex_indices;
    std::vector<float> root_positions;
    std::vector<float> pelvis_orientations;
    std::vector<float> torso_positions;
    std::vector<float> torso_orientations;
    std::vector<float> vertices;
};

struct MotionAsset
{
    std::filesystem::path motion_asset_path;
    std::string display_name;
};

struct GarmentDistanceConstraints final
{
    std::vector<MeshEdge> colorized_edges;
    std::vector<MeshElementRange> color_ranges;
    std::vector<float> rest_lengths;

    bool is_valid() const
    {
        return !colorized_edges.empty() &&
               !color_ranges.empty() &&
               colorized_edges.size() == rest_lengths.size();
    }
};

enum class GarmentCategory : std::uint32_t
{
    Top,
    Bottom,
    FullBody,
};

struct GarmentMesh
{
    GarmentCategory garment_category = GarmentCategory::Top;
    std::vector<float> vertices;
    std::vector<std::uint32_t> triangle_vertex_indices;
    VertexFaceAdjacency adjacency;
    GarmentDistanceConstraints stretch_constraints;
    GarmentDistanceConstraints bending_constraints;
    std::vector<std::uint32_t> attachment_vertex_indices;
    glm::vec3 color{1.0f};
    glm::vec3 bounds_center{};
    float bounds_radius = 1.0f;
};

struct GarmentAsset
{
    std::filesystem::path garment_asset_path;
    std::string display_name;
};
