#include "gpu/bvh/MeshBvhBuilder.h"

#include "asset/MeshGeometryUtils.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

#include <glm/glm.hpp>

namespace {
constexpr std::size_t edge_vertex_count = 2u;
constexpr std::size_t triangle_vertex_count = 3u;

constexpr std::size_t leaf_size = 3u;
constexpr std::size_t shader_max_bvh_stack_depth = 32u;

constexpr std::uint8_t left_hand_part_label = 6u;
constexpr std::uint8_t right_hand_part_label = 7u;
constexpr std::uint8_t invalid_part_label = 0xFFu;
constexpr std::size_t body_part_label_count = 8u;
using LabelCounts = std::array<std::uint32_t, body_part_label_count>;

bool is_excluded_part(std::uint8_t part_label)
{
    return part_label == left_hand_part_label || part_label == right_hand_part_label;
}

void count_edge_label(std::map<MeshEdge, LabelCounts>& edge_part_label_counts,
                      std::uint32_t vertex_a,
                      std::uint32_t vertex_b,
                      std::uint8_t part_label)
{
    const MeshEdge edge = vertex_a < vertex_b ? MeshEdge{vertex_a, vertex_b} : MeshEdge{vertex_b, vertex_a};
    ++edge_part_label_counts[edge][part_label];
}

std::uint8_t find_most_frequent_label(const LabelCounts& counts)
{
    return static_cast<std::uint8_t>(std::max_element(counts.begin(), counts.end()) - counts.begin());
}

bool has_multiple_part_labels(std::uint32_t mask)
{
    return (mask & (mask - 1u)) != 0u;
}

bool contains_part_label(std::uint32_t mask, std::uint8_t part_label)
{
    return (mask & (1u << part_label)) != 0u;
}

double surface_area(const glm::vec3& min_bounds, const glm::vec3& max_bounds)
{
    const glm::vec3 aabb_size = glm::max(max_bounds - min_bounds, glm::vec3{0.0f});
    return 2.0 * (static_cast<double>(aabb_size.x) * aabb_size.y +
                  static_cast<double>(aabb_size.y) * aabb_size.z +
                  static_cast<double>(aabb_size.z) * aabb_size.x);
}

struct PrimitiveGroupStats final
{
    glm::vec3 min_bounds{std::numeric_limits<float>::max()};
    glm::vec3 max_bounds{std::numeric_limits<float>::lowest()};
    std::uint32_t count = 0u;

    void update(const glm::vec3& min, const glm::vec3& max)
    {
        min_bounds = glm::min(min_bounds, min);
        max_bounds = glm::max(max_bounds, max);
        ++count;
    }

    void merge(const PrimitiveGroupStats& other)
    {
        if (other.count == 0u) {
            return;
        }

        min_bounds = glm::min(min_bounds, other.min_bounds);
        max_bounds = glm::max(max_bounds, other.max_bounds);
        count += other.count;
    }

    double cost() const { return surface_area(min_bounds, max_bounds) * count; }
};

std::uint32_t find_longest_axis(const glm::vec3& aabb_size)
{
    if (aabb_size.x >= aabb_size.y && aabb_size.x >= aabb_size.z) {
        return 0;
    }
    if (aabb_size.y >= aabb_size.z) {
        return 1;
    }
    return 2;
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
    const std::uint32_t triangle_count = source_triangle_vertex_indices_.size() / triangle_vertex_count;
    triangle_part_labels_.resize(triangle_count, 0u);
    collision_triangle_indices_.reserve(triangle_count);
    for (std::uint32_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        collision_triangle_indices_.push_back(triangle_index);
    }
}

// BVH construction
Bvh MeshBvhBuilder::build_triangle_bvh()
{
    reset_build();
    make_triangle_primitives();
    build_bvh();

    std::vector<std::uint32_t> triangle_indices = std::move(bvh_.indices);
    bvh_.indices = make_triangle_vertex_indices(std::move(triangle_indices));
    return std::move(bvh_);
}

Bvh MeshBvhBuilder::build_vertex_bvh()
{
    reset_build();
    make_vertex_primitives();
    build_bvh();

    return std::move(bvh_);
}

Bvh MeshBvhBuilder::build_edge_bvh()
{
    reset_build();
    const std::vector<LabeledEdge> edges = make_labeled_edges();
    make_edge_primitives(edges);
    build_bvh();

    bvh_.indices = make_edge_vertex_indices(bvh_.indices, edges);

    return std::move(bvh_);
}

void MeshBvhBuilder::reset_build()
{
    primitives_.clear();
    bvh_ = {};
}

void MeshBvhBuilder::build_bvh()
{
    bvh_.nodes.reserve(primitives_.size() * 2u - 1u);
    BvhNode root;
    root.element_count = primitives_.size();
    bvh_.nodes.push_back(root);

    // Build nodes level by level
    std::uint32_t level_node_begin = 0u;
    while (level_node_begin < bvh_.nodes.size()) {
        bvh_.level_offsets.push_back(level_node_begin);

        const auto level_node_end = static_cast<std::uint32_t>(bvh_.nodes.size());
        for (std::uint32_t node_index = level_node_begin; node_index < level_node_end; ++node_index) {
            build_node(node_index);
        }
        level_node_begin = level_node_end;
    }
    bvh_.level_offsets.push_back(static_cast<std::uint32_t>(bvh_.nodes.size()));

    if (bvh_.level_offsets.size() > shader_max_bvh_stack_depth + 1u) {
        throw std::runtime_error("Failed to build BVH.");
    }

    bvh_.indices.reserve(primitives_.size());
    for (const BvhPrimitive& primitive : primitives_) {
        bvh_.indices.push_back(primitive.element_index);
    }
}

void MeshBvhBuilder::build_node(std::uint32_t node_index)
{
    BvhNode& node = bvh_.nodes[node_index];
    const std::uint32_t part_label_mask = update_node_bounds(node);

    // leaf node
    if (!has_multiple_part_labels(part_label_mask) && node.element_count <= leaf_size) {
        return;
    }

    // internal node
    const std::size_t partition_index = partition_primitives(node, part_label_mask);
    const std::uint32_t primitive_index_begin = node.first_element_index;
    const std::uint32_t primitive_index_end = primitive_index_begin + node.element_count;

    BvhNode left_child;
    left_child.first_element_index = primitive_index_begin;
    left_child.element_count = partition_index - primitive_index_begin;

    BvhNode right_child;
    right_child.first_element_index = partition_index;
    right_child.element_count = primitive_index_end - partition_index;

    node.left_child_index = static_cast<std::uint32_t>(bvh_.nodes.size());
    node.right_child_index = node.left_child_index + 1u;
    node.first_element_index = 0u;
    node.element_count = 0u;
    bvh_.nodes.push_back(left_child);
    bvh_.nodes.push_back(right_child);
}

std::uint32_t MeshBvhBuilder::update_node_bounds(BvhNode& node) const
{
    PrimitiveGroupStats node_bounds;
    std::uint32_t part_label_mask = 0u;
    const std::size_t primitive_index_end = node.first_element_index + node.element_count;

    for (std::size_t index = node.first_element_index; index < primitive_index_end; ++index) {
        const BvhPrimitive& primitive = primitives_[index];
        node_bounds.update(primitive.min_bounds, primitive.max_bounds);
        part_label_mask |= 1u << primitive.part_label;
    }

    node.bounds = {glm::vec4(node_bounds.min_bounds, 0.0f), glm::vec4(node_bounds.max_bounds, 0.0f)};
    return part_label_mask;
}

std::size_t MeshBvhBuilder::partition_primitives(const BvhNode& node, std::uint32_t part_label_mask)
{
    // multiple part labels: split by part labels
    if (has_multiple_part_labels(part_label_mask)) {
        return partition_primitives_by_part_labels(node, choose_left_part_labels(node, part_label_mask));
    }
    // single part label: split by longest axis
    else {
        return partition_primitives_by_axis(node);
    }
}

std::uint32_t MeshBvhBuilder::choose_left_part_labels(const BvhNode& node,
                                                      std::uint32_t part_label_mask) const
{
    const std::size_t primitive_index_end = node.first_element_index + node.element_count;

    // Collect bounds and counts by part label
    std::array<PrimitiveGroupStats, body_part_label_count> group_by_label;
    for (std::size_t index = node.first_element_index; index < primitive_index_end; ++index) {
        const BvhPrimitive& primitive = primitives_[index];
        group_by_label[primitive.part_label].update(primitive.min_bounds, primitive.max_bounds);
    }

    double best_cost = std::numeric_limits<double>::max();
    std::uint32_t best_left_labels = 0u;
    std::uint32_t left_labels = (part_label_mask - 1u) & part_label_mask;

    // Find the lowest-cost part label split
    for (; left_labels != 0u; left_labels = (left_labels - 1u) & part_label_mask) {
        const std::uint32_t right_labels = part_label_mask ^ left_labels;
        if (left_labels > right_labels) {
            continue;
        }

        PrimitiveGroupStats left_group;
        PrimitiveGroupStats right_group;

        for (std::size_t label_index = 0; label_index < group_by_label.size(); ++label_index) {
            const auto part_label = static_cast<std::uint8_t>(label_index);
            auto& group = contains_part_label(left_labels, part_label) ? left_group : right_group;
            group.merge(group_by_label[label_index]);
        }

        const double cost = left_group.cost() + right_group.cost();
        if (best_left_labels == 0u || cost < best_cost) {
            best_cost = cost;
            best_left_labels = left_labels;
        }
    }
    return best_left_labels;
}

std::size_t MeshBvhBuilder::partition_primitives_by_part_labels(const BvhNode& node,
                                                                std::uint32_t left_part_label_mask)
{
    const auto primitive_begin = primitives_.begin() + node.first_element_index;
    const auto primitive_end = primitive_begin + node.element_count;
    const auto partition_iterator =
        std::partition(primitive_begin, primitive_end, [left_part_label_mask](const BvhPrimitive& primitive) {
            return contains_part_label(left_part_label_mask, primitive.part_label);
        });
    return static_cast<std::size_t>(partition_iterator - primitives_.begin());
}

std::size_t MeshBvhBuilder::partition_primitives_by_axis(const BvhNode& node)
{
    const glm::vec3 aabb_size = glm::vec3(node.bounds.max_bounds - node.bounds.min_bounds);
    const std::uint32_t axis = find_longest_axis(aabb_size);
    const std::size_t partition_index = node.first_element_index + node.element_count / 2u;

    std::nth_element(primitives_.begin() + node.first_element_index,
                     primitives_.begin() + partition_index,
                     primitives_.begin() + node.first_element_index + node.element_count,
                     [axis](const BvhPrimitive& lhs, const BvhPrimitive& rhs) {
                         return lhs.center[axis] < rhs.center[axis];
                     });
    return partition_index;
}

// Primitive construction
void MeshBvhBuilder::make_triangle_primitives()
{
    primitives_.reserve(collision_triangle_indices_.size());

    for (const std::uint32_t triangle_index : collision_triangle_indices_) {
        const std::size_t index_base = triangle_index * triangle_vertex_count;

        const std::uint32_t vertex_index_a = source_triangle_vertex_indices_[index_base];
        const std::uint32_t vertex_index_b = source_triangle_vertex_indices_[index_base + 1u];
        const std::uint32_t vertex_index_c = source_triangle_vertex_indices_[index_base + 2u];
        const glm::vec3 position_a = get_vertex_position(source_vertex_positions_, vertex_index_a);
        const glm::vec3 position_b = get_vertex_position(source_vertex_positions_, vertex_index_b);
        const glm::vec3 position_c = get_vertex_position(source_vertex_positions_, vertex_index_c);

        const glm::vec3 center = (position_a + position_b + position_c) / 3.0f;
        const glm::vec3 min_bounds = glm::min(glm::min(position_a, position_b), position_c);
        const glm::vec3 max_bounds = glm::max(glm::max(position_a, position_b), position_c);

        primitives_.push_back(
            {triangle_index, center, min_bounds, max_bounds, triangle_part_labels_[triangle_index]});
    }
}

void MeshBvhBuilder::make_vertex_primitives()
{
    const std::vector<std::uint8_t> vertex_part_labels = make_vertex_part_labels();

    primitives_.reserve(vertex_count_);

    for (std::uint32_t vertex_index = 0; vertex_index < vertex_count_; ++vertex_index) {
        if (vertex_part_labels[vertex_index] == invalid_part_label) {
            continue;
        }

        const glm::vec3 position = get_vertex_position(source_vertex_positions_, vertex_index);
        primitives_.push_back({vertex_index, position, position, position, vertex_part_labels[vertex_index]});
    }
}

void MeshBvhBuilder::make_edge_primitives(const std::vector<LabeledEdge>& edges)
{
    primitives_.reserve(edges.size());
    for (std::uint32_t edge_index = 0; edge_index < edges.size(); ++edge_index) {
        const LabeledEdge& edge = edges[edge_index];
        const glm::vec3 position_a = get_vertex_position(source_vertex_positions_, edge.edge.vertex_a);
        const glm::vec3 position_b = get_vertex_position(source_vertex_positions_, edge.edge.vertex_b);

        const glm::vec3 min_bounds = glm::min(position_a, position_b);
        const glm::vec3 max_bounds = glm::max(position_a, position_b);

        primitives_.push_back(
            {edge_index, (position_a + position_b) * 0.5f, min_bounds, max_bounds, edge.part_label});
    }
}

// Part label assignment
std::vector<std::uint8_t> MeshBvhBuilder::make_vertex_part_labels() const
{
    // 1. count part labels for all collsion triangles
    std::vector<LabelCounts> label_counts(vertex_count_, LabelCounts{});

    for (const std::uint32_t triangle_index : collision_triangle_indices_) {
        const std::uint8_t part_label = triangle_part_labels_[triangle_index];
        const std::size_t index_base = triangle_index * triangle_vertex_count;
        ++label_counts[source_triangle_vertex_indices_[index_base]][part_label];
        ++label_counts[source_triangle_vertex_indices_[index_base + 1u]][part_label];
        ++label_counts[source_triangle_vertex_indices_[index_base + 2u]][part_label];
    }

    // 2. get most frequent part label for each vertex
    // Vertex part label: most frequent across adjacent triangles
    std::vector<std::uint8_t> vertex_part_labels(vertex_count_);
    for (std::uint32_t vertex_index = 0; vertex_index < vertex_count_; ++vertex_index) {
        const LabelCounts& counts = label_counts[vertex_index];
        const std::uint8_t best_label = find_most_frequent_label(counts);
        vertex_part_labels[vertex_index] = counts[best_label] == 0u ? invalid_part_label : best_label;
    }
    return vertex_part_labels;
}

std::vector<MeshBvhBuilder::LabeledEdge> MeshBvhBuilder::make_labeled_edges() const
{
    std::map<MeshEdge, LabelCounts> edge_part_label_counts;

    // 1. count edge part labels for all collision triangles
    for (const std::uint32_t triangle_index : collision_triangle_indices_) {
        const std::uint8_t part_label = triangle_part_labels_[triangle_index];
        const std::size_t index_base = triangle_index * triangle_vertex_count;
        const std::uint32_t vertex_a = source_triangle_vertex_indices_[index_base];
        const std::uint32_t vertex_b = source_triangle_vertex_indices_[index_base + 1u];
        const std::uint32_t vertex_c = source_triangle_vertex_indices_[index_base + 2u];

        count_edge_label(edge_part_label_counts, vertex_a, vertex_b, part_label);
        count_edge_label(edge_part_label_counts, vertex_b, vertex_c, part_label);
        count_edge_label(edge_part_label_counts, vertex_c, vertex_a, part_label);
    }

    // 2. get most frequent part label for each edge
    // Edge part label: most frequent across adjacent triangles
    std::vector<LabeledEdge> labeled_edges;
    labeled_edges.reserve(edge_part_label_counts.size());
    for (const auto& [edge, label_counts] : edge_part_label_counts) {
        labeled_edges.push_back({edge, find_most_frequent_label(label_counts)});
    }
    return labeled_edges;
}

// Index construction
std::vector<std::uint32_t> MeshBvhBuilder::make_triangle_vertex_indices(
    std::vector<std::uint32_t> triangle_indices) const
{
    triangle_indices.reserve(triangle_indices.size() + excluded_triangle_indices_.size());
    triangle_indices.insert(triangle_indices.end(),
                            excluded_triangle_indices_.begin(),
                            excluded_triangle_indices_.end());

    std::vector<std::uint32_t> triangle_vertex_indices;
    triangle_vertex_indices.reserve(triangle_indices.size() * triangle_vertex_count);

    for (const std::uint32_t triangle_index : triangle_indices) {
        const std::size_t index_base = triangle_index * triangle_vertex_count;
        triangle_vertex_indices.push_back(source_triangle_vertex_indices_[index_base]);
        triangle_vertex_indices.push_back(source_triangle_vertex_indices_[index_base + 1u]);
        triangle_vertex_indices.push_back(source_triangle_vertex_indices_[index_base + 2u]);
    }

    return triangle_vertex_indices;
}

std::vector<std::uint32_t> MeshBvhBuilder::make_edge_vertex_indices(
    const std::vector<std::uint32_t>& ordered_edge_indices,
    const std::vector<LabeledEdge>& edges)
{
    std::vector<std::uint32_t> edge_vertex_indices;
    edge_vertex_indices.reserve(ordered_edge_indices.size() * edge_vertex_count);
    for (const std::uint32_t edge_index : ordered_edge_indices) {
        const MeshEdge edge = edges[edge_index].edge;
        edge_vertex_indices.push_back(edge.vertex_a);
        edge_vertex_indices.push_back(edge.vertex_b);
    }
    return edge_vertex_indices;
}
