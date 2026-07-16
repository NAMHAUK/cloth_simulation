#pragma once

#include "gpu/bvh/BvhDataTypes.h"
#include "gpu/bvh/BvhBuildUtils.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

class TriangleBvhBuilder final {
public:
    TriangleBvhBuilder(std::uint32_t vertex_count,
                       const std::vector<std::uint32_t>& triangle_indices,
                       const std::vector<float>& vertices);
    TriangleBvhBuilder(std::uint32_t vertex_count,
                       const std::vector<std::uint32_t>& triangle_indices,
                       const std::vector<float>& vertices,
                       const std::vector<std::uint8_t>& triangle_part_labels);

    TriangleBvhData build_triangle_bvh();

private:
    bool build_triangle_items();
    bool has_part_labels() const;
    void write_triangle_index_payload(const std::vector<std::uint32_t>& ordered_triangle_indices,
                                      std::vector<std::uint32_t>& triangle_indices) const;

    std::uint32_t vertex_count_ = 0;
    const std::vector<std::uint32_t>& source_triangle_indices_;
    const std::vector<float>& vertices_;
    const std::vector<std::uint8_t>* triangle_part_labels_ = nullptr;
    std::vector<bvh_build::DefaultBodyElement> triangle_items_;
    std::vector<bvh_build::BvhBuildNode> build_nodes_;
};
