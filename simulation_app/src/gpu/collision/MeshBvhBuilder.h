#pragma once

#include "gpu/collision/BvhDataTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

class MeshBvhBuilder final {
public:
    MeshBvhBuilder(std::uint32_t vertex_count,
                   const std::vector<std::uint32_t>& triangle_indices,
                   const std::vector<float>& vertices);
    MeshBvhBuilder(std::uint32_t vertex_count,
                   const std::vector<std::uint32_t>& triangle_indices,
                   const std::vector<float>& vertices,
                   const std::vector<std::uint8_t>& triangle_part_labels);

    MeshBvhData build_mesh_bvh();

private:
    struct TriangleBuildItem final {
        std::uint32_t triangle_index = 0;
        glm::vec3 center{};
        glm::vec3 min_bounds{};
        glm::vec3 max_bounds{};
        std::uint8_t part_label = 0;
    };

    struct BvhBuildNode final {
        glm::vec3 min_bounds{};
        glm::vec3 max_bounds{};
        std::uint32_t left_child_index = invalid_mesh_bvh_node;
        std::uint32_t right_child_index = invalid_mesh_bvh_node;
        std::uint32_t first_triangle_index = 0;
        std::uint32_t triangle_count = 0;
    };

    struct NextBvhLevel final {
        explicit NextBvhLevel(const BvhNodeRange& current_level): first_node(current_level.first_node + current_level.node_count)
        {
            node_indices.reserve(static_cast<std::size_t>(current_level.node_count) * 2u);
        }

        std::vector<std::uint32_t> node_indices;
        std::uint32_t first_node = 0;
    };

    bool build_triangle_items();
    
    std::uint32_t build_bvh_tree(std::size_t begin, std::size_t end, std::vector<std::uint32_t>& triangle_indices);
    bool has_part_labels() const;
    std::uint32_t compute_part_label_mask(std::size_t begin, std::size_t end) const;
    std::uint32_t find_best_part_label_split_mask(std::size_t begin, std::size_t end, std::uint32_t part_label_mask) const;
    std::size_t partition_triangle_items_by_part_labels(std::size_t begin, std::size_t end, std::uint32_t left_part_label_mask);
    std::size_t partition_triangle_items(std::size_t begin, std::size_t end, const glm::vec3& extent);
    void write_leaf_node_data(BvhBuildNode& node, std::size_t begin, std::size_t end, std::vector<std::uint32_t>& triangle_indices) const;
    void compute_node_bounds(std::size_t begin, std::size_t end, glm::vec3& min_bounds, glm::vec3& max_bounds) const;

    void write_level_ordered_bvh_data(std::uint32_t root_node_index, MeshBvhData& result) const;
    void append_bvh_node(std::vector<MeshBvhNode>& result_nodes,
                         std::uint32_t build_node_index,
                         NextBvhLevel& next_level) const;

    std::uint32_t vertex_count_ = 0;
    const std::vector<std::uint32_t>& source_triangle_indices_;
    const std::vector<float>& vertices_;
    const std::vector<std::uint8_t>* triangle_part_labels_ = nullptr;
    std::vector<TriangleBuildItem> triangle_items_;
    std::vector<BvhBuildNode> build_nodes_;
};
