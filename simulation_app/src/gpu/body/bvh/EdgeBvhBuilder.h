#pragma once

#include "asset/AssetDataTypes.h"
#include "gpu/body/bvh/BvhDataTypes.h"
#include "gpu/body/bvh/BvhBuildUtils.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class EdgeBvhBuilder final {
public:
    EdgeBvhBuilder(std::uint32_t vertex_count,
                   const std::vector<std::uint32_t>& triangle_indices,
                   const std::vector<float>& vertices,
                   const std::vector<std::uint8_t>& triangle_part_labels);

    EdgeBvhData build_edge_bvh();

private:
    bool build_edge_items();
    bool has_part_labels() const;
    bool build_edge_part_labels(std::vector<std::uint8_t>& edge_part_labels) const;
    void write_edge_index_payload(const std::vector<std::uint32_t>& ordered_edge_indices,
                                  std::vector<std::uint32_t>& edge_vertex_indices) const;

    std::uint32_t vertex_count_ = 0;
    const std::vector<std::uint32_t>& source_triangle_indices_;
    const std::vector<float>& vertices_;
    const std::vector<std::uint8_t>& triangle_part_labels_;
    std::vector<MeshEdge> source_edges_;
    std::vector<bvh_build::DefaultBodyElement> edge_items_;
    std::vector<bvh_build::BvhBuildNode> build_nodes_;
};
