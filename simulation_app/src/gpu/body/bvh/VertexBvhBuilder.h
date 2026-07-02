#pragma once

#include "gpu/body/bvh/BvhDataTypes.h"
#include "gpu/body/bvh/BvhBuildUtils.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

class VertexBvhBuilder final {
public:
    VertexBvhBuilder(std::uint32_t vertex_count,
                     const std::vector<std::uint32_t>& triangle_indices,
                     const std::vector<float>& vertices,
                     const std::vector<std::uint8_t>& triangle_part_labels);

    VertexBvhData build_vertex_bvh();

private:
    bool build_vertex_items();
    bool has_part_labels() const;
    bool build_vertex_part_labels(std::vector<std::uint8_t>& vertex_part_labels) const;

    std::uint32_t vertex_count_ = 0;
    const std::vector<std::uint32_t>& source_triangle_indices_;
    const std::vector<float>& vertices_;
    const std::vector<std::uint8_t>& triangle_part_labels_;
    std::vector<bvh_build::DefaultBodyElement> vertex_items_;
    std::vector<bvh_build::BvhBuildNode> build_nodes_;
};
