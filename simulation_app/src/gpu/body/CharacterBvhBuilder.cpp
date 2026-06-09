#include "gpu/body/CharacterBvhBuilder.h"

#include "asset/MeshGeometryUtils.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace {
constexpr std::uint32_t triangle_vertex_count = 3;
constexpr std::uint32_t character_bvh_leaf_size = 8;

struct TriangleBuildItem final {
    std::uint32_t triangle_index = 0;
    glm::vec3 center{};
    glm::vec3 min_bounds{};
    glm::vec3 max_bounds{};
};

struct BvhBuildNode final {
    glm::vec3 min_bounds{};
    glm::vec3 max_bounds{};
    std::uint32_t left_child = invalid_character_bvh_node;
    std::uint32_t right_child = invalid_character_bvh_node;
    std::uint32_t first_triangle = 0;
    std::uint32_t triangle_count = 0;
};

bool is_valid_bounds(const glm::vec3& min_bounds, const glm::vec3& max_bounds)
{
    return std::isfinite(min_bounds.x) && std::isfinite(min_bounds.y) && std::isfinite(min_bounds.z) &&
           std::isfinite(max_bounds.x) && std::isfinite(max_bounds.y) && std::isfinite(max_bounds.z);
}

// BVH 빌드 전처리: 각 triangle의 중심과 bound 계산
bool build_triangle_items(std::uint32_t vertex_count,
                          const std::vector<std::uint32_t>& triangle_indices,
                          const std::vector<float>& vertices,
                          std::vector<TriangleBuildItem>& items)
{
    // mesh data 검증
    if (vertex_count == 0 ||
        triangle_indices.empty() ||
        triangle_indices.size() % triangle_vertex_count != 0u ||
        vertices.size() < static_cast<std::size_t>(vertex_count) * triangle_vertex_count) {
        return false;
    }

    const std::uint32_t triangle_count = static_cast<std::uint32_t>(triangle_indices.size() / triangle_vertex_count);
    items.clear();
    items.reserve(triangle_count);

    // 각 triangle에 대해 중심 위치, bound 계산
    for (std::uint32_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        const std::size_t index_base = static_cast<std::size_t>(triangle_index) * triangle_vertex_count;
        
        glm::vec3 center_sum(0.0f);
        glm::vec3 min_bounds(std::numeric_limits<float>::max());
        glm::vec3 max_bounds(std::numeric_limits<float>::lowest());

        // 각 triangle의 vertex 순회
        for (std::uint32_t index_offset = 0; index_offset < triangle_vertex_count; ++index_offset) {
            const std::uint32_t vertex_index = triangle_indices[index_base + index_offset];
            if (vertex_index >= vertex_count) {
                return false;
            }

            const glm::vec3 position = get_vertex_position(vertices, vertex_index);
            center_sum += position;
            min_bounds = glm::min(min_bounds, position);
            max_bounds = glm::max(max_bounds, position);
        }

        if (!is_valid_bounds(min_bounds, max_bounds)) {
            return false;
        }

        items.push_back({
            triangle_index,
            center_sum / static_cast<float>(triangle_vertex_count),
            min_bounds,
            max_bounds
        });
    }

    return true;
}
// 
void compute_range_bounds(const std::vector<TriangleBuildItem>& items,
                          std::size_t begin,
                          std::size_t end,
                          glm::vec3& min_bounds,
                          glm::vec3& max_bounds)
{
    min_bounds = glm::vec3(std::numeric_limits<float>::max());
    max_bounds = glm::vec3(std::numeric_limits<float>::lowest());

    for (std::size_t item_index = begin; item_index < end; ++item_index) {
        min_bounds = glm::min(min_bounds, items[item_index].min_bounds);
        max_bounds = glm::max(max_bounds, items[item_index].max_bounds);
    }
}

std::uint32_t find_longest_axis(const glm::vec3& extent)
{
    if (extent.x >= extent.y && extent.x >= extent.z) {
        return 0;
    }
    if (extent.y >= extent.z) {
        return 1;
    }
    return 2;
}

std::uint32_t build_bvh_tree(std::vector<TriangleBuildItem>& items,
                             std::size_t begin,
                             std::size_t end,
                             std::vector<BvhBuildNode>& build_nodes,
                             std::vector<std::uint32_t>& ordered_triangle_indices)
{
    const std::uint32_t node_index = static_cast<std::uint32_t>(build_nodes.size());
    build_nodes.emplace_back();

    BvhBuildNode& node = build_nodes.back();
    compute_range_bounds(items, begin, end, node.min_bounds, node.max_bounds);

    const std::size_t triangle_count = end - begin;
    if (triangle_count <= character_bvh_leaf_size) {
        node.first_triangle = static_cast<std::uint32_t>(ordered_triangle_indices.size());
        node.triangle_count = static_cast<std::uint32_t>(triangle_count);
        for (std::size_t item_index = begin; item_index < end; ++item_index) {
            ordered_triangle_indices.push_back(items[item_index].triangle_index);
        }
        return node_index;
    }

    const glm::vec3 extent = node.max_bounds - node.min_bounds;
    const std::uint32_t axis = find_longest_axis(extent);
    const std::size_t middle = begin + triangle_count / 2u;
    std::nth_element(items.begin() + static_cast<std::ptrdiff_t>(begin),
                     items.begin() + static_cast<std::ptrdiff_t>(middle),
                     items.begin() + static_cast<std::ptrdiff_t>(end),
                     [axis](const TriangleBuildItem& lhs, const TriangleBuildItem& rhs) {
                         return lhs.center[axis] < rhs.center[axis];
                     });

    const std::uint32_t left_child = build_bvh_tree(items, begin, middle, build_nodes, ordered_triangle_indices);
    const std::uint32_t right_child = build_bvh_tree(items, middle, end, build_nodes, ordered_triangle_indices);
    build_nodes[node_index].left_child = left_child;
    build_nodes[node_index].right_child = right_child;
    return node_index;
}

void build_breadth_order(const std::vector<BvhBuildNode>& build_nodes,
                         std::uint32_t root_node_index,
                         std::vector<std::uint32_t>& breadth_order,
                         std::vector<BvhBoundsUpdateLevelRange>& level_ranges)
{
    breadth_order.clear();
    level_ranges.clear();
    breadth_order.reserve(build_nodes.size());

    std::vector<std::uint32_t> current_level{root_node_index};

    while (!current_level.empty()) {
        level_ranges.push_back({
            static_cast<std::uint32_t>(breadth_order.size()),
            static_cast<std::uint32_t>(current_level.size())
        });
        breadth_order.insert(breadth_order.end(), current_level.begin(), current_level.end());

        std::vector<std::uint32_t> next_level;
        for (const std::uint32_t node_index : current_level) {
            const BvhBuildNode& node = build_nodes[node_index];
            if (node.left_child != invalid_character_bvh_node) {
                next_level.push_back(node.left_child);
            }
            if (node.right_child != invalid_character_bvh_node) {
                next_level.push_back(node.right_child);
            }
        }
        current_level = std::move(next_level);
    }
}

void write_level_ordered_nodes(const std::vector<BvhBuildNode>& build_nodes,
                               const std::vector<std::uint32_t>& breadth_order,
                               const std::vector<BvhBoundsUpdateLevelRange>& level_ranges,
                               CharacterBvhBuildResult& result)
{
    std::vector<std::uint32_t> final_indices(build_nodes.size(), invalid_character_bvh_node);
    for (std::size_t final_index = 0; final_index < breadth_order.size(); ++final_index) {
        final_indices[breadth_order[final_index]] = static_cast<std::uint32_t>(final_index);
    }

    result.nodes.resize(build_nodes.size());
    for (const std::uint32_t build_node_index : breadth_order) {
        const BvhBuildNode& build_node = build_nodes[build_node_index];
        CharacterBvhNode& node = result.nodes[final_indices[build_node_index]];
        node.min_bounds = glm::vec4(build_node.min_bounds, 0.0f);
        node.max_bounds = glm::vec4(build_node.max_bounds, 0.0f);

        if (build_node.triangle_count > 0) {
            node.metadata = glm::uvec4(
                invalid_character_bvh_node,
                invalid_character_bvh_node,
                build_node.first_triangle,
                build_node.triangle_count
            );
        } else {
            node.metadata = glm::uvec4(final_indices[build_node.left_child], final_indices[build_node.right_child], 0u, 0u);
        }
    }

    result.bounds_update_level_ranges.clear();
    result.bounds_update_level_ranges.reserve(level_ranges.size());
    for (auto range = level_ranges.rbegin(); range != level_ranges.rend(); ++range) {
        result.bounds_update_level_ranges.push_back(*range);
    }
}

void write_triangle_indices(const std::vector<std::uint32_t>& source_indices,
                            const std::vector<std::uint32_t>& ordered_triangle_indices,
                            std::vector<std::uint32_t>& triangle_indices)
{
    triangle_indices.clear();
    triangle_indices.reserve(source_indices.size());

    for (const std::uint32_t triangle_index : ordered_triangle_indices) {
        const std::size_t index_base = static_cast<std::size_t>(triangle_index) * triangle_vertex_count;
        triangle_indices.push_back(source_indices[index_base]);
        triangle_indices.push_back(source_indices[index_base + 1u]);
        triangle_indices.push_back(source_indices[index_base + 2u]);
    }
}
}

bool CharacterBvhBuildResult::is_valid(std::uint32_t triangle_count) const
{
    return triangle_count > 0 &&
           root_node_index < nodes.size() &&
           !nodes.empty() &&
           !bounds_update_level_ranges.empty() &&
           triangle_indices.size() == static_cast<std::size_t>(triangle_count) * triangle_vertex_count;
}

CharacterBvhBuildResult build_character_bvh(std::uint32_t vertex_count,
                                            const std::vector<std::uint32_t>& triangle_indices,
                                            const std::vector<float>& vertices)
{
    CharacterBvhBuildResult result;

    std::vector<TriangleBuildItem> items;
    if (!build_triangle_items(vertex_count, triangle_indices, vertices, items)) {
        return result;
    }

    std::vector<BvhBuildNode> build_nodes;
    std::vector<std::uint32_t> ordered_triangle_indices;
    const std::size_t leaf_count = (items.size() + character_bvh_leaf_size - 1u) / character_bvh_leaf_size;
    build_nodes.reserve(leaf_count * 2u - 1u);
    ordered_triangle_indices.reserve(items.size());

    const std::uint32_t root_node_index = build_bvh_tree(items, 0u, items.size(), build_nodes, ordered_triangle_indices);
    std::vector<std::uint32_t> breadth_order;
    std::vector<BvhBoundsUpdateLevelRange> level_ranges;
    build_breadth_order(build_nodes, root_node_index, breadth_order, level_ranges);

    write_triangle_indices(triangle_indices, ordered_triangle_indices, result.triangle_indices);
    write_level_ordered_nodes(build_nodes, breadth_order, level_ranges, result);
    result.root_node_index = 0;
    return result;
}
