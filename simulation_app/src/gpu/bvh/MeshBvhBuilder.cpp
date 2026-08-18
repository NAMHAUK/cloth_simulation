#include "gpu/bvh/MeshBvhBuilder.h"

#include "asset/MeshGeometryUtils.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>

#include <glm/glm.hpp>

namespace {
constexpr std::size_t edge_vertex_count = 2u;
constexpr std::size_t triangle_vertex_count = 3u;
constexpr std::uint8_t left_hand_part_label = 6u;
constexpr std::uint8_t right_hand_part_label = 7u;
constexpr std::uint8_t invalid_part_label = 0xFFu;
using LabelCounts = std::array<std::uint32_t, bvh_build::body_part_label_count>;

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
Bvh MeshBvhBuilder::build_triangle_bvh() const
{
    std::vector<bvh_build::BvhPrimitive> primitives = make_triangle_primitives();
    Bvh bvh = bvh_build::build_bvh(primitives);

    std::vector<std::uint32_t> triangle_indices;
    triangle_indices.reserve(primitives.size());
    for (const bvh_build::BvhPrimitive& primitive : primitives) {
        triangle_indices.push_back(primitive.element_index);
    }
    bvh.indices = make_triangle_vertex_indices(std::move(triangle_indices));

    const auto triangle_count =
        static_cast<std::uint32_t>(source_triangle_vertex_indices_.size() / triangle_vertex_count);
    if (!bvh_build::is_valid_triangle_bvh(bvh, triangle_count)) {
        throw std::runtime_error("Failed to build triangle BVH.");
    }
    return bvh;
}

Bvh MeshBvhBuilder::build_vertex_bvh() const
{
    std::vector<bvh_build::BvhPrimitive> primitives = make_vertex_primitives();
    Bvh bvh = bvh_build::build_bvh(primitives);

    bvh.indices.reserve(primitives.size());
    for (const bvh_build::BvhPrimitive& primitive : primitives) {
        bvh.indices.push_back(primitive.element_index);
    }

    if (!bvh_build::is_valid_vertex_bvh(bvh, vertex_count_)) {
        throw std::runtime_error("Failed to build vertex BVH.");
    }
    return bvh;
}

Bvh MeshBvhBuilder::build_edge_bvh() const
{
    const std::vector<LabeledEdge> edges = make_labeled_edges();
    std::vector<bvh_build::BvhPrimitive> primitives = make_edge_primitives(edges);
    Bvh bvh = bvh_build::build_bvh(primitives);

    std::vector<std::uint32_t> edge_indices;
    edge_indices.reserve(primitives.size());
    for (const bvh_build::BvhPrimitive& primitive : primitives) {
        edge_indices.push_back(primitive.element_index);
    }
    bvh.indices = make_edge_vertex_indices(edge_indices, edges);

    if (!bvh_build::is_valid_edge_bvh(bvh)) {
        throw std::runtime_error("Failed to build edge BVH.");
    }
    return bvh;
}

// Primitive construction
std::vector<bvh_build::BvhPrimitive> MeshBvhBuilder::make_triangle_primitives() const
{
    std::vector<bvh_build::BvhPrimitive> primitives;
    primitives.reserve(collision_triangle_indices_.size());

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

        primitives.push_back(
            {triangle_index, center, min_bounds, max_bounds, triangle_part_labels_[triangle_index]});
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

std::vector<bvh_build::BvhPrimitive> MeshBvhBuilder::make_edge_primitives(
    const std::vector<LabeledEdge>& edges) const
{
    std::vector<bvh_build::BvhPrimitive> primitives;
    primitives.reserve(edges.size());
    for (std::uint32_t edge_index = 0; edge_index < edges.size(); ++edge_index) {
        const LabeledEdge& edge = edges[edge_index];
        const glm::vec3 position_a = get_vertex_position(source_vertex_positions_, edge.edge.vertex_a);
        const glm::vec3 position_b = get_vertex_position(source_vertex_positions_, edge.edge.vertex_b);

        const glm::vec3 min_bounds = glm::min(position_a, position_b);
        const glm::vec3 max_bounds = glm::max(position_a, position_b);

        primitives.push_back(
            {edge_index, (position_a + position_b) * 0.5f, min_bounds, max_bounds, edge.part_label});
    }

    return primitives;
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
