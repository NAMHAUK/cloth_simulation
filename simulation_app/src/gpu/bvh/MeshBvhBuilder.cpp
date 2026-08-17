#include "gpu/bvh/MeshBvhBuilder.h"

#include "asset/MeshGeometryUtils.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <glm/glm.hpp>

namespace {
constexpr std::size_t edge_vertex_count = 2u;
constexpr std::size_t triangle_vertex_count = 3u;
constexpr std::uint8_t left_hand_part_label = 6u;
constexpr std::uint8_t right_hand_part_label = 7u;
constexpr std::uint8_t invalid_part_label = 0xFFu;

bool is_excluded_part(std::uint8_t part_label)
{
    return part_label == left_hand_part_label || part_label == right_hand_part_label;
}

MeshEdge make_ordered_edge(std::uint32_t vertex_a, std::uint32_t vertex_b)
{
    if (vertex_a < vertex_b) {
        return {vertex_a, vertex_b};
    }
    return {vertex_b, vertex_a};
}

std::uint64_t edge_key(MeshEdge edge)
{
    return (static_cast<std::uint64_t>(edge.vertex_a) << 32u) | edge.vertex_b;
}
}

// Initialization
MeshBvhBuilder::MeshBvhBuilder(const CharacterMotion& motion,
                               const std::vector<std::uint8_t>& triangle_part_labels)
    : vertex_count_(motion.vertex_count),
      source_triangle_vertex_indices_(motion.triangle_vertex_indices),
      source_vertex_positions_(motion.vertices),
      triangle_part_labels_(triangle_part_labels)
{
    // Body: build triangle index list (separate collision and excluded groups)
    collision_triangle_indices_.reserve(triangle_part_labels.size());
    excluded_triangle_indices_.reserve(triangle_part_labels.size());
    for (std::uint32_t triangle_index = 0; triangle_index < triangle_part_labels.size(); ++triangle_index) {
        if (is_excluded_part(triangle_part_labels[triangle_index])) {
            excluded_triangle_indices_.push_back(triangle_index);
        } else {
            collision_triangle_indices_.push_back(triangle_index);
        }
    }
}

MeshBvhBuilder::MeshBvhBuilder(const GarmentMesh& mesh)
    : vertex_count_(static_cast<std::uint32_t>(mesh.vertices.size() / position_components)),
      source_triangle_vertex_indices_(mesh.triangle_vertex_indices),
      source_vertex_positions_(mesh.vertices)
{   
    // Garment: build triangle index list
    const auto triangle_count =
        static_cast<std::uint32_t>(source_triangle_vertex_indices_.size() / triangle_vertex_count);
    collision_triangle_indices_.reserve(triangle_count);
    triangle_part_labels_.resize(triangle_count, 0u);
    for (std::uint32_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        collision_triangle_indices_.push_back(triangle_index);
    }
}

// BVH construction
TriangleBvhData MeshBvhBuilder::build_triangle_bvh() const
{
    bvh_build::BvhTree tree = bvh_build::build_bvh(make_triangle_primitives());

    TriangleBvhData bvh_data;
    bvh_data.collision_triangle_count = static_cast<std::uint32_t>(tree.ordered_source_indices.size());
    bvh_data.triangle_vertex_indices.reserve(source_triangle_vertex_indices_.size());
    append_triangle_vertex_indices(tree.ordered_source_indices, bvh_data.triangle_vertex_indices);
    append_triangle_vertex_indices(excluded_triangle_indices_, bvh_data.triangle_vertex_indices);
    bvh_data.nodes = std::move(tree.nodes);
    bvh_data.levels = std::move(tree.levels);
    const auto triangle_count =
        static_cast<std::uint32_t>(source_triangle_vertex_indices_.size() / triangle_vertex_count);
    if (!bvh_data.is_valid(triangle_count)) {
        throw std::runtime_error("Failed to build triangle BVH.");
    }
    return bvh_data;
}

VertexBvhData MeshBvhBuilder::build_vertex_bvh() const
{
    bvh_build::BvhTree tree = bvh_build::build_bvh(make_vertex_primitives());

    VertexBvhData bvh_data;
    bvh_data.vertex_indices = std::move(tree.ordered_source_indices);
    bvh_data.nodes = std::move(tree.nodes);
    bvh_data.levels = std::move(tree.levels);
    if (!bvh_data.is_valid(vertex_count_)) {
        throw std::runtime_error("Failed to build vertex BVH.");
    }
    return bvh_data;
}

EdgeBvhData MeshBvhBuilder::build_edge_bvh() const
{
    EdgeBvhData bvh_data;
    EdgePrimitiveSet edge_primitives = make_edge_primitives();
    bvh_build::BvhTree tree = bvh_build::build_bvh(std::move(edge_primitives.primitives));
    write_edge_index_payload(tree.ordered_source_indices,
                             edge_primitives.source_edges,
                             bvh_data.edge_vertex_indices);
    bvh_data.nodes = std::move(tree.nodes);
    bvh_data.levels = std::move(tree.levels);
    if (!bvh_data.is_valid()) {
        throw std::runtime_error("Failed to build edge BVH.");
    }
    return bvh_data;
}

// Primitive construction
std::vector<bvh_build::BvhPrimitive> MeshBvhBuilder::make_triangle_primitives() const
{
    std::vector<bvh_build::BvhPrimitive> primitives;
    primitives.reserve(collision_triangle_indices_.size());

    for (const std::uint32_t triangle_index : collision_triangle_indices_) {
        const std::size_t index_base = triangle_index * triangle_vertex_count;

        glm::vec3 center_sum(0.0f);
        glm::vec3 min_bounds(std::numeric_limits<float>::max());
        glm::vec3 max_bounds(std::numeric_limits<float>::lowest());

        for (std::size_t index_offset = 0; index_offset < triangle_vertex_count; ++index_offset) {
            const std::uint32_t vertex_index = source_triangle_vertex_indices_[index_base + index_offset];
            const glm::vec3 position = get_vertex_position(source_vertex_positions_, vertex_index);
            center_sum += position;
            min_bounds = glm::min(min_bounds, position);
            max_bounds = glm::max(max_bounds, position);
        }

        primitives.push_back({triangle_index,
                              center_sum / static_cast<float>(triangle_vertex_count),
                              min_bounds,
                              max_bounds,
                              triangle_part_labels_[triangle_index]});
    }

    return primitives;
}

std::vector<bvh_build::BvhPrimitive> MeshBvhBuilder::make_vertex_primitives() const
{
    const std::vector<std::uint8_t> vertex_part_labels = make_vertex_part_labels();

    std::vector<bvh_build::BvhPrimitive> primitives;
    primitives.reserve(vertex_count_);

    for (std::uint32_t vertex_index = 0; vertex_index < vertex_count_; ++vertex_index) {
        if (vertex_part_labels[vertex_index] == invalid_part_label) {
            continue;
        }

        const glm::vec3 position = get_vertex_position(source_vertex_positions_, vertex_index);
        primitives.push_back({vertex_index, position, position, position, vertex_part_labels[vertex_index]});
    }

    return primitives;
}

MeshBvhBuilder::EdgePrimitiveSet MeshBvhBuilder::make_edge_primitives() const
{
    EdgePrimitiveSet primitive_set;
    const std::vector<std::uint32_t> collision_triangle_vertex_indices =
        make_collision_triangle_vertex_indices();
    primitive_set.source_edges =
        build_unique_triangle_edges(vertex_count_, collision_triangle_vertex_indices);

    const std::vector<std::uint8_t> edge_part_labels = make_edge_part_labels(primitive_set.source_edges);

    primitive_set.primitives.reserve(primitive_set.source_edges.size());
    for (std::uint32_t edge_index = 0; edge_index < primitive_set.source_edges.size(); ++edge_index) {
        const MeshEdge edge = primitive_set.source_edges[edge_index];
        const glm::vec3 position_a = get_vertex_position(source_vertex_positions_, edge.vertex_a);
        const glm::vec3 position_b = get_vertex_position(source_vertex_positions_, edge.vertex_b);
        const glm::vec3 min_bounds = glm::min(position_a, position_b);
        const glm::vec3 max_bounds = glm::max(position_a, position_b);
        primitive_set.primitives.push_back({edge_index,
                                            (position_a + position_b) * 0.5f,
                                            min_bounds,
                                            max_bounds,
                                            edge_part_labels[edge_index]});
    }

    return primitive_set;
}

// Part label assignment
std::vector<std::uint8_t> MeshBvhBuilder::make_vertex_part_labels() const
{
    using LabelCounts = std::array<std::uint32_t, bvh_build::body_part_label_count>;
    std::vector<LabelCounts> label_counts(vertex_count_, LabelCounts{});

    for (const std::uint32_t triangle_index : collision_triangle_indices_) {
        const std::uint8_t part_label = triangle_part_labels_[triangle_index];
        const std::size_t index_base = triangle_index * triangle_vertex_count;
        for (std::size_t index_offset = 0; index_offset < triangle_vertex_count; ++index_offset) {
            const std::uint32_t vertex_index = source_triangle_vertex_indices_[index_base + index_offset];
            ++label_counts[vertex_index][part_label];
        }
    }

    std::vector<std::uint8_t> vertex_part_labels(vertex_count_);
    for (std::uint32_t vertex_index = 0; vertex_index < vertex_count_; ++vertex_index) {
        const LabelCounts& counts = label_counts[vertex_index];
        const auto best_count = std::max_element(counts.begin(), counts.end());
        const auto best_label = static_cast<std::uint8_t>(best_count - counts.begin());
        vertex_part_labels[vertex_index] = *best_count == 0u ? invalid_part_label : best_label;
    }
    return vertex_part_labels;
}

std::vector<std::uint8_t> MeshBvhBuilder::make_edge_part_labels(
    const std::vector<MeshEdge>& source_edges) const
{
    using LabelCounts = std::array<std::uint32_t, bvh_build::body_part_label_count>;
    std::vector<LabelCounts> label_counts(source_edges.size(), LabelCounts{});
    std::unordered_map<std::uint64_t, std::uint32_t> edge_indices_by_key;
    edge_indices_by_key.reserve(source_edges.size());
    for (std::uint32_t edge_index = 0; edge_index < source_edges.size(); ++edge_index) {
        edge_indices_by_key.emplace(edge_key(source_edges[edge_index]), edge_index);
    }

    for (const std::uint32_t triangle_index : collision_triangle_indices_) {
        const std::uint8_t part_label = triangle_part_labels_[triangle_index];
        const std::size_t index_base = triangle_index * triangle_vertex_count;
        const std::uint32_t vertex_a = source_triangle_vertex_indices_[index_base];
        const std::uint32_t vertex_b = source_triangle_vertex_indices_[index_base + 1u];
        const std::uint32_t vertex_c = source_triangle_vertex_indices_[index_base + 2u];
        const MeshEdge triangle_edges[triangle_vertex_count] = {
            make_ordered_edge(vertex_a, vertex_b),
            make_ordered_edge(vertex_b, vertex_c),
            make_ordered_edge(vertex_c, vertex_a),
        };
        for (const MeshEdge edge : triangle_edges) {
            ++label_counts[edge_indices_by_key.at(edge_key(edge))][part_label];
        }
    }

    std::vector<std::uint8_t> edge_part_labels(source_edges.size());
    for (std::uint32_t edge_index = 0; edge_index < source_edges.size(); ++edge_index) {
        const LabelCounts& counts = label_counts[edge_index];
        const auto best_label = std::max_element(counts.begin(), counts.end()) - counts.begin();
        edge_part_labels[edge_index] = static_cast<std::uint8_t>(best_label);
    }
    return edge_part_labels;
}

// Index payload construction
std::vector<std::uint32_t> MeshBvhBuilder::make_collision_triangle_vertex_indices() const
{
    std::vector<std::uint32_t> triangle_vertex_indices;
    triangle_vertex_indices.reserve(collision_triangle_indices_.size() * triangle_vertex_count);
    append_triangle_vertex_indices(collision_triangle_indices_, triangle_vertex_indices);
    return triangle_vertex_indices;
}

void MeshBvhBuilder::append_triangle_vertex_indices(const std::vector<std::uint32_t>& source_triangle_indices,
                                                    std::vector<std::uint32_t>& triangle_vertex_indices) const
{
    for (const std::uint32_t triangle_index : source_triangle_indices) {
        const std::size_t index_base = triangle_index * triangle_vertex_count;
        triangle_vertex_indices.push_back(source_triangle_vertex_indices_[index_base]);
        triangle_vertex_indices.push_back(source_triangle_vertex_indices_[index_base + 1u]);
        triangle_vertex_indices.push_back(source_triangle_vertex_indices_[index_base + 2u]);
    }
}

void MeshBvhBuilder::write_edge_index_payload(const std::vector<std::uint32_t>& ordered_edge_indices,
                                              const std::vector<MeshEdge>& source_edges,
                                              std::vector<std::uint32_t>& edge_vertex_indices)
{
    edge_vertex_indices.reserve(ordered_edge_indices.size() * edge_vertex_count);
    for (const std::uint32_t edge_index : ordered_edge_indices) {
        const MeshEdge edge = source_edges[edge_index];
        edge_vertex_indices.push_back(edge.vertex_a);
        edge_vertex_indices.push_back(edge.vertex_b);
    }
}
