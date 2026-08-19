#pragma once

#include "asset/AssetDataTypes.h"
#include "gpu/bvh/BvhBuildUtils.h"
#include "gpu/bvh/BvhDataTypes.h"

#include <cstdint>
#include <vector>

class MeshBvhBuilder final
{
public:
    MeshBvhBuilder(const CharacterMotion& motion, const std::vector<std::uint8_t>& triangle_part_labels);
    explicit MeshBvhBuilder(const GarmentMesh& mesh);

    Bvh build_triangle_bvh() const;
    Bvh build_vertex_bvh() const;
    Bvh build_edge_bvh() const;

private:
    struct LabeledEdge final
    {
        MeshEdge edge;
        std::uint8_t part_label = 0;
    };

    std::vector<bvh_build::BvhPrimitive> make_triangle_primitives() const;
    std::vector<bvh_build::BvhPrimitive> make_vertex_primitives() const;
    std::vector<bvh_build::BvhPrimitive> make_edge_primitives(const std::vector<LabeledEdge>& edges) const;

    std::vector<std::uint8_t> make_vertex_part_labels() const;
    std::vector<LabeledEdge> make_labeled_edges() const;

    std::vector<std::uint32_t> make_triangle_vertex_indices(
        std::vector<std::uint32_t> triangle_indices) const;
    static std::vector<std::uint32_t> make_edge_vertex_indices(
        const std::vector<std::uint32_t>& ordered_edge_indices,
        const std::vector<LabeledEdge>& edges);

    std::uint32_t vertex_count_ = 0;
    const std::vector<std::uint32_t>& source_triangle_vertex_indices_;
    const std::vector<float>& source_vertex_positions_;
    std::vector<std::uint32_t> collision_triangle_indices_;
    std::vector<std::uint32_t> excluded_triangle_indices_;
    std::vector<std::uint8_t> triangle_part_labels_;
};
