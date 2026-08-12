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
constexpr std::uint32_t edge_vertex_count = 2;
constexpr std::uint32_t triangle_vertex_count = 3;
constexpr std::uint32_t vertex_position_component_count = 3;
constexpr std::uint32_t bvh_leaf_size = 3;
constexpr std::uint32_t body_bvh_excluded_part_mask = (1u << 6u) | (1u << 7u);
constexpr std::uint8_t invalid_part_label = 0xFFu;

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

MeshBvhBuilder::MeshBvhBuilder(const CharacterMotion& motion,
                               const std::vector<std::uint8_t>& triangle_part_labels)
    : vertex_count_(motion.vertex_count),
      source_triangle_indices_(motion.triangle_vertex_indices),
      vertices_(motion.vertices),
      triangle_part_labels_(&triangle_part_labels)
{}

MeshBvhBuilder::MeshBvhBuilder(const GarmentMesh& mesh)
    : vertex_count_(static_cast<std::uint32_t>(mesh.vertices.size() / vertex_position_component_count)),
      source_triangle_indices_(mesh.triangle_vertex_indices),
      vertices_(mesh.vertices)
{}

TriangleBvhData MeshBvhBuilder::build_triangle_bvh() const
{
    TriangleBvhData result;
    auto primitives = make_triangle_primitives();
    bvh_build::BvhTree tree = bvh_build::build_bvh(std::move(primitives), bvh_leaf_size, has_part_labels());
    if (tree.nodes.empty()) {
        throw std::runtime_error("Failed to build triangle BVH.");
    }
    result.collision_triangle_count = static_cast<std::uint32_t>(tree.ordered_primitive_indices.size());
    result.triangle_indices.reserve(source_triangle_indices_.size());
    write_triangle_index_payload(tree.ordered_primitive_indices, result.triangle_indices);
    write_triangle_index_payload(make_excluded_triangle_ids(), result.triangle_indices);
    result.nodes = std::move(tree.nodes);
    result.node_ranges_by_level = std::move(tree.node_ranges_by_level);
    const auto triangle_count =
        static_cast<std::uint32_t>(source_triangle_indices_.size() / triangle_vertex_count);
    if (!result.is_valid(triangle_count)) {
        throw std::runtime_error("Failed to build triangle BVH.");
    }
    return result;
}

VertexBvhData MeshBvhBuilder::build_vertex_bvh() const
{
    VertexBvhData result;
    auto primitives = make_vertex_primitives();
    bvh_build::BvhTree tree = bvh_build::build_bvh(std::move(primitives), bvh_leaf_size, has_part_labels());
    result.vertex_ids = std::move(tree.ordered_primitive_indices);
    result.nodes = std::move(tree.nodes);
    result.node_ranges_by_level = std::move(tree.node_ranges_by_level);
    if (!result.is_valid(vertex_count_)) {
        throw std::runtime_error("Failed to build vertex BVH.");
    }
    return result;
}

EdgeBvhData MeshBvhBuilder::build_edge_bvh() const
{
    EdgeBvhData result;
    EdgePrimitiveSet edge_primitives = make_edge_primitives();
    bvh_build::BvhTree tree =
        bvh_build::build_bvh(std::move(edge_primitives.primitives), bvh_leaf_size, has_part_labels());
    write_edge_index_payload(tree.ordered_primitive_indices,
                             edge_primitives.source_edges,
                             result.edge_vertex_indices);
    result.nodes = std::move(tree.nodes);
    result.node_ranges_by_level = std::move(tree.node_ranges_by_level);
    if (!result.is_valid()) {
        throw std::runtime_error("Failed to build edge BVH.");
    }
    return result;
}

std::vector<bvh_build::BvhPrimitive> MeshBvhBuilder::make_triangle_primitives() const
{
    if (vertex_count_ == 0 ||
        source_triangle_indices_.empty() ||
        source_triangle_indices_.size() % triangle_vertex_count != 0u ||
        vertices_.size() < static_cast<std::size_t>(vertex_count_) * vertex_position_component_count) {
        return {};
    }

    const std::uint32_t triangle_count =
        static_cast<std::uint32_t>(source_triangle_indices_.size() / triangle_vertex_count);
    if (has_part_labels() && triangle_part_labels_->size() != triangle_count) {
        return {};
    }

    std::vector<bvh_build::BvhPrimitive> primitives;
    primitives.reserve(triangle_count);

    for (std::uint32_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        const std::size_t index_base = static_cast<std::size_t>(triangle_index) * triangle_vertex_count;

        glm::vec3 center_sum(0.0f);
        glm::vec3 min_bounds(std::numeric_limits<float>::max());
        glm::vec3 max_bounds(std::numeric_limits<float>::lowest());

        for (std::uint32_t index_offset = 0; index_offset < triangle_vertex_count; ++index_offset) {
            const std::uint32_t vertex_index = source_triangle_indices_[index_base + index_offset];
            if (vertex_index >= vertex_count_) {
                return {};
            }

            const glm::vec3 position = get_vertex_position(vertices_, vertex_index);
            center_sum += position;
            min_bounds = glm::min(min_bounds, position);
            max_bounds = glm::max(max_bounds, position);
        }

        if (!bvh_build::is_valid_bounds(min_bounds, max_bounds)) {
            return {};
        }

        const std::uint8_t part_label = has_part_labels() ? (*triangle_part_labels_)[triangle_index] : 0u;
        if (part_label >= bvh_build::body_part_label_count) {
            return {};
        }
        if (is_part_excluded(part_label)) {
            continue;
        }

        primitives.push_back({triangle_index,
                              center_sum / static_cast<float>(triangle_vertex_count),
                              min_bounds,
                              max_bounds,
                              part_label});
    }

    return primitives;
}

std::vector<bvh_build::BvhPrimitive> MeshBvhBuilder::make_vertex_primitives() const
{
    if (vertex_count_ == 0 ||
        vertices_.size() < static_cast<std::size_t>(vertex_count_) * vertex_position_component_count) {
        return {};
    }

    std::vector<std::uint8_t> vertex_part_labels;
    if (has_part_labels()) {
        vertex_part_labels = make_vertex_part_labels();
        if (vertex_part_labels.empty()) {
            return {};
        }
    }

    std::vector<bvh_build::BvhPrimitive> primitives;
    primitives.reserve(vertex_count_);

    for (std::uint32_t vertex_id = 0; vertex_id < vertex_count_; ++vertex_id) {
        if (has_part_labels() && vertex_part_labels[vertex_id] == invalid_part_label) {
            continue;
        }

        const glm::vec3 position = get_vertex_position(vertices_, vertex_id);
        if (!bvh_build::is_valid_bounds(position, position)) {
            return {};
        }
        const std::uint8_t part_label = has_part_labels() ? vertex_part_labels[vertex_id] : 0u;
        primitives.push_back({vertex_id, position, position, position, part_label});
    }

    return primitives;
}

MeshBvhBuilder::EdgePrimitiveSet MeshBvhBuilder::make_edge_primitives() const
{
    if (vertices_.size() < static_cast<std::size_t>(vertex_count_) * vertex_position_component_count) {
        return {};
    }

    EdgePrimitiveSet result;
    const std::vector<std::uint32_t> collision_triangle_indices = make_collision_triangle_indices();
    result.source_edges = build_unique_triangle_edges(vertex_count_, collision_triangle_indices);
    if (result.source_edges.empty()) {
        return result;
    }

    std::vector<std::uint8_t> edge_part_labels;
    if (has_part_labels()) {
        edge_part_labels = make_edge_part_labels(result.source_edges);
        if (edge_part_labels.empty()) {
            return {};
        }
    }

    result.primitives.reserve(result.source_edges.size());
    for (std::uint32_t edge_index = 0; edge_index < result.source_edges.size(); ++edge_index) {
        const MeshEdge edge = result.source_edges[edge_index];
        const glm::vec3 position_a = get_vertex_position(vertices_, edge.vertex_a);
        const glm::vec3 position_b = get_vertex_position(vertices_, edge.vertex_b);
        const glm::vec3 min_bounds = glm::min(position_a, position_b);
        const glm::vec3 max_bounds = glm::max(position_a, position_b);
        if (!bvh_build::is_valid_bounds(min_bounds, max_bounds)) {
            return {};
        }

        const std::uint8_t part_label = has_part_labels() ? edge_part_labels[edge_index] : 0u;
        result.primitives.push_back(
            {edge_index, (position_a + position_b) * 0.5f, min_bounds, max_bounds, part_label});
    }

    return result;
}

std::vector<std::uint8_t> MeshBvhBuilder::make_vertex_part_labels() const
{
    if (triangle_part_labels_ == nullptr || source_triangle_indices_.size() % triangle_vertex_count != 0u) {
        return {};
    }

    const std::size_t triangle_count = source_triangle_indices_.size() / triangle_vertex_count;
    if (triangle_part_labels_->size() != triangle_count) {
        return {};
    }

    using LabelCounts = std::array<std::uint32_t, bvh_build::body_part_label_count>;
    std::vector<LabelCounts> label_counts(vertex_count_, LabelCounts{});

    for (std::size_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        const std::uint8_t part_label = (*triangle_part_labels_)[triangle_index];
        if (part_label >= bvh_build::body_part_label_count) {
            return {};
        }
        if (is_part_excluded(part_label)) {
            continue;
        }

        const std::size_t index_base = triangle_index * triangle_vertex_count;
        for (std::uint32_t index_offset = 0; index_offset < triangle_vertex_count; ++index_offset) {
            const std::uint32_t vertex_id = source_triangle_indices_[index_base + index_offset];
            if (vertex_id >= vertex_count_) {
                return {};
            }
            ++label_counts[vertex_id][part_label];
        }
    }

    std::vector<std::uint8_t> vertex_part_labels(vertex_count_);
    for (std::uint32_t vertex_id = 0; vertex_id < vertex_count_; ++vertex_id) {
        const LabelCounts& counts = label_counts[vertex_id];
        if (std::all_of(counts.begin(), counts.end(), [](std::uint32_t count) { return count == 0u; })) {
            vertex_part_labels[vertex_id] = invalid_part_label;
            continue;
        }
        const auto best_label = std::max_element(counts.begin(), counts.end()) - counts.begin();
        vertex_part_labels[vertex_id] = static_cast<std::uint8_t>(best_label);
    }
    return vertex_part_labels;
}

std::vector<std::uint8_t> MeshBvhBuilder::make_edge_part_labels(
    const std::vector<MeshEdge>& source_edges) const
{
    if (triangle_part_labels_ == nullptr) {
        return {};
    }

    const std::size_t triangle_count = source_triangle_indices_.size() / triangle_vertex_count;
    if (triangle_part_labels_->size() != triangle_count) {
        return {};
    }

    using LabelCounts = std::array<std::uint32_t, bvh_build::body_part_label_count>;
    std::vector<LabelCounts> label_counts(source_edges.size(), LabelCounts{});
    std::unordered_map<std::uint64_t, std::uint32_t> edge_indices_by_key;
    edge_indices_by_key.reserve(source_edges.size());
    for (std::uint32_t edge_index = 0; edge_index < source_edges.size(); ++edge_index) {
        edge_indices_by_key.emplace(edge_key(source_edges[edge_index]), edge_index);
    }

    for (std::size_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        const std::uint8_t part_label = (*triangle_part_labels_)[triangle_index];
        if (part_label >= bvh_build::body_part_label_count) {
            return {};
        }
        if (is_part_excluded(part_label)) {
            continue;
        }

        const std::size_t index_base = triangle_index * triangle_vertex_count;
        const std::uint32_t vertex_a = source_triangle_indices_[index_base];
        const std::uint32_t vertex_b = source_triangle_indices_[index_base + 1u];
        const std::uint32_t vertex_c = source_triangle_indices_[index_base + 2u];
        const MeshEdge triangle_edges[triangle_vertex_count] = {
            make_ordered_edge(vertex_a, vertex_b),
            make_ordered_edge(vertex_b, vertex_c),
            make_ordered_edge(vertex_c, vertex_a),
        };
        for (const MeshEdge edge : triangle_edges) {
            if (edge.vertex_a == edge.vertex_b) {
                continue;
            }
            const auto iter = edge_indices_by_key.find(edge_key(edge));
            if (iter == edge_indices_by_key.end()) {
                return {};
            }
            ++label_counts[iter->second][part_label];
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

bool MeshBvhBuilder::has_part_labels() const
{
    return triangle_part_labels_ != nullptr && !triangle_part_labels_->empty();
}

std::vector<std::uint32_t> MeshBvhBuilder::make_collision_triangle_indices() const
{
    if (!has_part_labels()) {
        return source_triangle_indices_;
    }

    std::vector<std::uint32_t> triangle_indices;
    triangle_indices.reserve(source_triangle_indices_.size());
    const std::size_t triangle_count = source_triangle_indices_.size() / triangle_vertex_count;
    for (std::size_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        const std::uint8_t part_label = (*triangle_part_labels_)[triangle_index];
        if (part_label >= bvh_build::body_part_label_count) {
            return {};
        }
        if (is_part_excluded(part_label)) {
            continue;
        }

        const std::size_t index_base = triangle_index * triangle_vertex_count;
        triangle_indices.push_back(source_triangle_indices_[index_base]);
        triangle_indices.push_back(source_triangle_indices_[index_base + 1u]);
        triangle_indices.push_back(source_triangle_indices_[index_base + 2u]);
    }
    return triangle_indices;
}

std::vector<std::uint32_t> MeshBvhBuilder::make_excluded_triangle_ids() const
{
    std::vector<std::uint32_t> triangle_ids;
    if (!has_part_labels()) {
        return triangle_ids;
    }

    const std::size_t triangle_count = source_triangle_indices_.size() / triangle_vertex_count;
    triangle_ids.reserve(triangle_count);
    for (std::uint32_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        if (is_part_excluded((*triangle_part_labels_)[triangle_index])) {
            triangle_ids.push_back(triangle_index);
        }
    }
    return triangle_ids;
}

bool MeshBvhBuilder::is_part_excluded(std::uint8_t part_label) const
{
    return (body_bvh_excluded_part_mask & (1u << part_label)) != 0u;
}

void MeshBvhBuilder::write_triangle_index_payload(const std::vector<std::uint32_t>& ordered_triangle_indices,
                                                  std::vector<std::uint32_t>& triangle_indices) const
{
    for (const std::uint32_t triangle_index : ordered_triangle_indices) {
        const std::size_t index_base = static_cast<std::size_t>(triangle_index) * triangle_vertex_count;
        triangle_indices.push_back(source_triangle_indices_[index_base]);
        triangle_indices.push_back(source_triangle_indices_[index_base + 1u]);
        triangle_indices.push_back(source_triangle_indices_[index_base + 2u]);
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
