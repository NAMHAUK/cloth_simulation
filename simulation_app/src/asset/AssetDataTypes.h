#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

inline constexpr std::size_t position_components = 3u;

enum class BodyPartLabel : std::uint8_t
{
    Torso = 0u,
    Head = 1u,
    LeftArm = 2u,
    RightArm = 3u,
    LeftLeg = 4u,
    RightLeg = 5u,
    LeftHand = 6u,
    RightHand = 7u,
    Count,
};

constexpr std::uint8_t body_part_label_value(BodyPartLabel label)
{
    return static_cast<std::uint8_t>(label);
}

inline constexpr std::size_t body_part_label_count = body_part_label_value(BodyPartLabel::Count);

constexpr bool is_hand_body_part_label(std::uint8_t label)
{
    return label == body_part_label_value(BodyPartLabel::LeftHand) ||
           label == body_part_label_value(BodyPartLabel::RightHand);
}

struct VertexTriangleAdjacency final
{
    std::vector<std::uint32_t> offsets;
    std::vector<std::uint32_t> triangle_indices;
    std::uint32_t triangle_count = 0;
};

struct MeshEdge final
{
    std::uint32_t vertex_a = 0;
    std::uint32_t vertex_b = 0;

    bool operator==(const MeshEdge& other) const
    {
        return vertex_a == other.vertex_a && vertex_b == other.vertex_b;
    }

    bool operator<(const MeshEdge& other) const
    {
        if (vertex_a != other.vertex_a) {
            return vertex_a < other.vertex_a;
        }
        return vertex_b < other.vertex_b;
    }
};

struct ConstraintColorState final
{
    std::uint32_t start_index = 0;
    std::uint32_t count = 0;
};

struct ColorizedMeshEdges final
{
    std::vector<MeshEdge> edges;
    std::vector<ConstraintColorState> color_states;
};

struct CharacterMotion
{
    float fps = 0.0f;
    std::uint32_t frame_count = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
    std::vector<std::uint32_t> triangle_vertex_indices;
    std::vector<float> pelvis_positions;
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
    std::vector<ConstraintColorState> color_states;
    std::vector<float> rest_lengths;

    bool is_valid() const
    {
        return !colorized_edges.empty() &&
               !color_states.empty() &&
               colorized_edges.size() == rest_lengths.size();
    }
};

enum class GarmentCategory : std::uint32_t
{
    Top,
    Bottom,
    FullBody,
};

enum GarmentLayer : std::size_t
{
    Lower = 0u,
    Upper = 1u,
};

struct GarmentMesh
{
    GarmentCategory garment_category = GarmentCategory::Top;
    std::vector<float> vertices;
    std::vector<std::uint32_t> triangle_vertex_indices;
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
