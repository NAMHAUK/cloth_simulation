#pragma once

#include "asset/AssetDataTypes.h"
#include "gpu/bvh/BvhBuildUtils.h"
#include "gpu/bvh/BvhDataTypes.h"

#include <cstdint>
#include <vector>

inline constexpr std::uint32_t body_bvh_excluded_part_mask = (1u << 6u) | (1u << 7u);

class MeshBvhBuilder final
{
public:
    MeshBvhBuilder(std::uint32_t vertex_count,
                   const std::vector<std::uint32_t>& triangle_indices,
                   const std::vector<float>& vertices);
    MeshBvhBuilder(std::uint32_t vertex_count,
                   const std::vector<std::uint32_t>& triangle_indices,
                   const std::vector<float>& vertices,
                   const std::vector<std::uint8_t>& triangle_part_labels,
                   std::uint32_t excluded_part_mask);

    TriangleBvhData build_triangle_bvh() const;
    VertexBvhData build_vertex_bvh() const;
    EdgeBvhData build_edge_bvh() const;

private:
    struct EdgePrimitiveSet final
    {
        std::vector<MeshEdge> source_edges;
        std::vector<bvh_build::BvhPrimitive> primitives;
    };

    std::vector<bvh_build::BvhPrimitive> make_triangle_primitives() const;
    std::vector<bvh_build::BvhPrimitive> make_vertex_primitives() const;
    EdgePrimitiveSet make_edge_primitives() const;
    std::vector<std::uint8_t> make_vertex_part_labels() const;
    std::vector<std::uint8_t> make_edge_part_labels(const std::vector<MeshEdge>& source_edges) const;
    std::vector<std::uint32_t> make_collision_triangle_indices() const;
    std::vector<std::uint32_t> make_excluded_triangle_ids() const;
    bool has_part_labels() const;
    bool is_part_excluded(std::uint8_t part_label) const;
    void write_triangle_index_payload(const std::vector<std::uint32_t>& ordered_triangle_indices,
                                      std::vector<std::uint32_t>& triangle_indices) const;
    static void write_edge_index_payload(const std::vector<std::uint32_t>& ordered_edge_indices,
                                         const std::vector<MeshEdge>& source_edges,
                                         std::vector<std::uint32_t>& edge_vertex_indices);

    std::uint32_t vertex_count_ = 0;
    const std::vector<std::uint32_t>& source_triangle_indices_;
    const std::vector<float>& vertices_;
    const std::vector<std::uint8_t>* triangle_part_labels_ = nullptr;
    std::uint32_t excluded_part_mask_ = 0;
};
