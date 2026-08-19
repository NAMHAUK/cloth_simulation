#pragma once

#include "asset/AssetDataTypes.h"
#include "gpu/bvh/BvhDataTypes.h"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

class MeshBvhBuilder final
{
public:
    MeshBvhBuilder(const CharacterMotion& motion, const std::vector<std::uint8_t>& triangle_part_labels);
    explicit MeshBvhBuilder(const GarmentMesh& mesh);

    Bvh build_triangle_bvh();
    Bvh build_vertex_bvh();
    Bvh build_edge_bvh();

private:
    struct BvhPrimitive final
    {
        std::uint32_t element_index = 0;
        glm::vec3 center{};
        glm::vec3 min_bounds{};
        glm::vec3 max_bounds{};
        std::uint8_t part_label = 0;
    };

    struct LabeledEdge final
    {
        MeshEdge edge;
        std::uint8_t part_label = 0;
    };

    // BVH construction
    void reset_build();
    void build_bvh();
    void build_node(std::uint32_t node_index);
    std::uint32_t update_node_bounds(BvhNode& node) const;
    std::size_t split_primitives(const BvhNode& node, std::uint32_t part_label_mask);
    std::uint32_t choose_part_split(const BvhNode& node, std::uint32_t part_label_mask) const;
    std::size_t partition_primitives_by_part_labels(const BvhNode& node, std::uint32_t left_part_label_mask);
    std::size_t partition_primitives(const BvhNode& node);

    // Primitive construction
    void make_triangle_primitives();
    void make_vertex_primitives();
    void make_edge_primitives(const std::vector<LabeledEdge>& edges);

    // Part label assignment
    std::vector<std::uint8_t> make_vertex_part_labels() const;
    std::vector<LabeledEdge> make_labeled_edges() const;

    // Index construction
    std::vector<std::uint32_t> make_triangle_vertex_indices(
        std::vector<std::uint32_t> triangle_indices) const;
    static std::vector<std::uint32_t> make_edge_vertex_indices(
        const std::vector<std::uint32_t>& ordered_edge_indices,
        const std::vector<LabeledEdge>& edges);

    // Source data
    std::uint32_t vertex_count_ = 0;
    const std::vector<std::uint32_t>& source_triangle_vertex_indices_;
    const std::vector<float>& source_vertex_positions_;
    std::vector<std::uint32_t> collision_triangle_indices_;
    std::vector<std::uint32_t> excluded_triangle_indices_;
    std::vector<std::uint8_t> triangle_part_labels_;

    // Build state
    std::vector<BvhPrimitive> primitives_;
    Bvh bvh_;
};
