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
constexpr std::size_t body_part_label_count = 8u;
constexpr std::size_t leaf_size = 3u;
constexpr std::size_t shader_max_bvh_stack_depth = 32u;
constexpr std::uint8_t left_hand_part_label = 6u;
constexpr std::uint8_t right_hand_part_label = 7u;
constexpr std::uint8_t invalid_part_label = 0xFFu;
using LabelCounts = std::array<std::uint32_t, body_part_label_count>;

struct PartLabelStats final
{
    glm::vec3 min_bounds{std::numeric_limits<float>::max()};
    glm::vec3 max_bounds{std::numeric_limits<float>::lowest()};
    std::uint32_t primitive_count = 0;
};

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

double surface_area(const glm::vec3& min_bounds, const glm::vec3& max_bounds)
{
    const glm::vec3 aabb_size = glm::max(max_bounds - min_bounds, glm::vec3{0.0f});
    return 2.0 * (static_cast<double>(aabb_size.x) * aabb_size.y +
                  static_cast<double>(aabb_size.y) * aabb_size.z +
                  static_cast<double>(aabb_size.z) * aabb_size.x);
}

double part_split_cost(const std::array<PartLabelStats, body_part_label_count>& stats_by_label,
                       std::uint32_t left_part_labels)
{
    PartLabelStats left_stats;
    PartLabelStats right_stats;
    for (std::size_t label = 0; label < stats_by_label.size(); ++label) {
        const PartLabelStats& source_stats = stats_by_label[label];
        if (source_stats.primitive_count == 0u) {
            continue;
        }

        const std::uint32_t label_mask = 1u << label;
        PartLabelStats& target_stats = (left_part_labels & label_mask) != 0u ? left_stats : right_stats;
        target_stats.min_bounds = glm::min(target_stats.min_bounds, source_stats.min_bounds);
        target_stats.max_bounds = glm::max(target_stats.max_bounds, source_stats.max_bounds);
        target_stats.primitive_count += source_stats.primitive_count;
    }

    return surface_area(left_stats.min_bounds, left_stats.max_bounds) * left_stats.primitive_count +
           surface_area(right_stats.min_bounds, right_stats.max_bounds) * right_stats.primitive_count;
}

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
    const auto triangle_count =
        static_cast<std::uint32_t>(source_triangle_vertex_indices_.size() / triangle_vertex_count);
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

    std::vector<std::uint32_t> triangle_indices;
    triangle_indices.reserve(primitives_.size());
    for (const BvhPrimitive& primitive : primitives_) {
        triangle_indices.push_back(primitive.element_index);
    }
    bvh_.indices = make_triangle_vertex_indices(std::move(triangle_indices));
    return std::move(bvh_);
}

Bvh MeshBvhBuilder::build_vertex_bvh()
{
    reset_build();
    make_vertex_primitives();
    build_bvh();

    bvh_.indices.reserve(primitives_.size());
    for (const BvhPrimitive& primitive : primitives_) {
        bvh_.indices.push_back(primitive.element_index);
    }

    return std::move(bvh_);
}

Bvh MeshBvhBuilder::build_edge_bvh()
{
    reset_build();
    const std::vector<LabeledEdge> edges = make_labeled_edges();
    make_edge_primitives(edges);
    build_bvh();

    std::vector<std::uint32_t> edge_indices;
    edge_indices.reserve(primitives_.size());
    for (const BvhPrimitive& primitive : primitives_) {
        edge_indices.push_back(primitive.element_index);
    }
    bvh_.indices = make_edge_vertex_indices(edge_indices, edges);

    return std::move(bvh_);
}

void MeshBvhBuilder::reset_build()
{
    primitives_.clear();
    bvh_ = {};
}

void MeshBvhBuilder::build_bvh()
{
    if (primitives_.empty()) {
        throw std::runtime_error("Cannot build an empty BVH.");
    }

    bvh_.nodes.reserve(primitives_.size() * 2u - 1u);
    BvhNode root;
    root.element_count = primitives_.size();
    bvh_.nodes.push_back(root);

    std::uint32_t level_begin = 0u;
    while (level_begin < bvh_.nodes.size()) {
        const auto level_end = static_cast<std::uint32_t>(bvh_.nodes.size());
        bvh_.level_offsets.push_back(level_begin);
        for (std::uint32_t node_index = level_begin; node_index < level_end; ++node_index) {
            build_node(node_index);
        }
        level_begin = level_end;
    }
    bvh_.level_offsets.push_back(static_cast<std::uint32_t>(bvh_.nodes.size()));

    if (bvh_.level_offsets.size() > shader_max_bvh_stack_depth + 1u) {
        throw std::runtime_error("Failed to build BVH.");
    }
}

void MeshBvhBuilder::build_node(std::uint32_t node_index)
{
    BvhNode& node = bvh_.nodes[node_index];
    const std::uint32_t part_label_mask = update_node_bounds(node);
    if (!has_multiple_part_labels(part_label_mask) && node.element_count <= leaf_size) {
        return;
    }

    const std::size_t middle = split_primitives(node, part_label_mask);
    const std::uint32_t primitive_begin = node.first_element_index;
    const std::uint32_t primitive_end = primitive_begin + node.element_count;

    BvhNode left_child;
    left_child.first_element_index = primitive_begin;
    left_child.element_count = middle - primitive_begin;

    BvhNode right_child;
    right_child.first_element_index = middle;
    right_child.element_count = primitive_end - middle;

    node.left_child_index = static_cast<std::uint32_t>(bvh_.nodes.size());
    node.right_child_index = node.left_child_index + 1u;
    node.first_element_index = 0u;
    node.element_count = 0u;
    bvh_.nodes.push_back(left_child);
    bvh_.nodes.push_back(right_child);
}

std::uint32_t MeshBvhBuilder::update_node_bounds(BvhNode& node) const
{
    glm::vec3 min_bounds{std::numeric_limits<float>::max()};
    glm::vec3 max_bounds{std::numeric_limits<float>::lowest()};
    std::uint32_t part_label_mask = 0u;
    const std::size_t primitive_end = node.first_element_index + node.element_count;

    for (std::size_t primitive_index = node.first_element_index; primitive_index < primitive_end;
         ++primitive_index) {
        const BvhPrimitive& primitive = primitives_[primitive_index];
        min_bounds = glm::min(min_bounds, primitive.min_bounds);
        max_bounds = glm::max(max_bounds, primitive.max_bounds);
        part_label_mask |= 1u << primitive.part_label;
    }

    node.bounds = {glm::vec4(min_bounds, 0.0f), glm::vec4(max_bounds, 0.0f)};
    return part_label_mask;
}

std::size_t MeshBvhBuilder::split_primitives(const BvhNode& node, std::uint32_t part_label_mask)
{
    if (has_multiple_part_labels(part_label_mask)) {
        const std::uint32_t left_part_label_mask = choose_part_split(node, part_label_mask);
        return partition_primitives_by_part_labels(node, left_part_label_mask);
    }
    return partition_primitives(node);
}

std::uint32_t MeshBvhBuilder::choose_part_split(const BvhNode& node, std::uint32_t part_label_mask) const
{
    std::array<PartLabelStats, body_part_label_count> stats_by_label;
    const std::size_t primitive_end = node.first_element_index + node.element_count;
    for (std::size_t primitive_index = node.first_element_index; primitive_index < primitive_end;
         ++primitive_index) {
        const BvhPrimitive& primitive = primitives_[primitive_index];
        PartLabelStats& stats = stats_by_label[primitive.part_label];
        stats.min_bounds = glm::min(stats.min_bounds, primitive.min_bounds);
        stats.max_bounds = glm::max(stats.max_bounds, primitive.max_bounds);
        ++stats.primitive_count;
    }

    double best_cost = std::numeric_limits<double>::max();
    std::uint32_t best_left_labels = 0u;
    for (std::uint32_t left_labels = (part_label_mask - 1u) & part_label_mask; left_labels != 0u;
         left_labels = (left_labels - 1u) & part_label_mask) {
        const std::uint32_t right_labels = part_label_mask ^ left_labels;
        if (left_labels > right_labels) {
            continue;
        }

        const double cost = part_split_cost(stats_by_label, left_labels);
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
    const auto middle =
        std::partition(primitive_begin, primitive_end, [left_part_label_mask](const BvhPrimitive& primitive) {
            return (left_part_label_mask & (1u << primitive.part_label)) != 0u;
        });
    return static_cast<std::size_t>(middle - primitives_.begin());
}

std::size_t MeshBvhBuilder::partition_primitives(const BvhNode& node)
{
    const glm::vec3 aabb_size = glm::vec3(node.bounds.max_bounds - node.bounds.min_bounds);
    const std::uint32_t axis = find_longest_axis(aabb_size);
    const std::size_t middle = node.first_element_index + node.element_count / 2u;

    std::nth_element(primitives_.begin() + node.first_element_index,
                     primitives_.begin() + middle,
                     primitives_.begin() + node.first_element_index + node.element_count,
                     [axis](const BvhPrimitive& lhs, const BvhPrimitive& rhs) {
                         return lhs.center[axis] < rhs.center[axis];
                     });
    return middle;
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
        const auto best_count = std::max_element(counts.begin(), counts.end());
        const auto best_label = static_cast<std::uint8_t>(best_count - counts.begin());
        vertex_part_labels[vertex_index] = *best_count == 0u ? invalid_part_label : best_label;
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
