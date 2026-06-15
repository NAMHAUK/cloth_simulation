#include "gpu/collision/MeshBvhBuilder.h"

#include "asset/MeshGeometryUtils.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace {
constexpr std::uint32_t triangle_vertex_count = 3;
constexpr std::uint32_t mesh_bvh_leaf_size = 8;

bool is_valid_bounds(const glm::vec3& min_bounds, const glm::vec3& max_bounds)
{
    return std::isfinite(min_bounds.x) && std::isfinite(min_bounds.y) && std::isfinite(min_bounds.z) &&
           std::isfinite(max_bounds.x) && std::isfinite(max_bounds.y) && std::isfinite(max_bounds.z);
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
}

MeshBvhBuilder::MeshBvhBuilder(std::uint32_t vertex_count,
                               const std::vector<std::uint32_t>& triangle_indices,
                               const std::vector<float>& vertices)
    : vertex_count_(vertex_count),
      source_triangle_indices_(triangle_indices),
      vertices_(vertices)
{
}

// 호출 함수 //
MeshBvhData MeshBvhBuilder::build_mesh_bvh()
{
    MeshBvhData result;

    if (!build_triangle_items()) {
        return result;
    }

    // 1. vector 메모리 미리 확보
    const std::size_t leaf_count = (triangle_items_.size() + mesh_bvh_leaf_size - 1u) / mesh_bvh_leaf_size;
    build_nodes_.reserve(leaf_count * 2u - 1u);
    result.triangle_indices.reserve(source_triangle_indices_.size());

    // 2. triangle들을 공간 기준으로 나눈 BVH 트리 build
    const std::uint32_t root_node_index = build_bvh_tree(0u, triangle_items_.size(), result.triangle_indices);
    
    // 3. 각 level 별 node range 계산
    write_level_ordered_bvh_data(root_node_index, result);
    
    result.root_node_index = 0;
    return result;
}

// BVH 빌드 전처리: 각 triangle의 중심과 bound 계산
bool MeshBvhBuilder::build_triangle_items()
{
    // mesh data 검증
    if (vertex_count_ == 0 ||
        source_triangle_indices_.empty() ||
        source_triangle_indices_.size() % triangle_vertex_count != 0u ||
        vertices_.size() < static_cast<std::size_t>(vertex_count_) * triangle_vertex_count) {
        return false;
    }

    const std::uint32_t triangle_count = static_cast<std::uint32_t>(source_triangle_indices_.size() / triangle_vertex_count);
    triangle_items_.clear();
    triangle_items_.reserve(triangle_count);

    // 각 triangle에 대해 중심 위치, bound 계산
    for (std::uint32_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        const std::size_t index_base = static_cast<std::size_t>(triangle_index) * triangle_vertex_count;
        
        glm::vec3 center_sum(0.0f);
        glm::vec3 min_bounds(std::numeric_limits<float>::max());
        glm::vec3 max_bounds(std::numeric_limits<float>::lowest());

        // 각 triangle의 vertex 순회
        for (std::uint32_t index_offset = 0; index_offset < triangle_vertex_count; ++index_offset) {
            const std::uint32_t vertex_index = source_triangle_indices_[index_base + index_offset];
            if (vertex_index >= vertex_count_) {
                return false;
            }

            const glm::vec3 position = get_vertex_position(vertices_, vertex_index);
            center_sum += position;
            min_bounds = glm::min(min_bounds, position);
            max_bounds = glm::max(max_bounds, position);
        }

        if (!is_valid_bounds(min_bounds, max_bounds)) {
            return false;
        }

        triangle_items_.push_back({
            triangle_index,
            center_sum / static_cast<float>(triangle_vertex_count),
            min_bounds,
            max_bounds
        });
    }

    return true;
}

// bvh tree build //
std::uint32_t MeshBvhBuilder::build_bvh_tree(std::size_t begin, std::size_t end, std::vector<std::uint32_t>& triangle_indices)
{   
    // 새로운 노드 생성 및 bound 계산
    const std::uint32_t node_index = static_cast<std::uint32_t>(build_nodes_.size());
    build_nodes_.emplace_back();

    BvhBuildNode& node = build_nodes_.back();
    compute_node_bounds(begin, end, node.min_bounds, node.max_bounds);

    // leaf node인 경우 
    const std::size_t triangle_count = end - begin;
    if (triangle_count <= mesh_bvh_leaf_size) {
        write_leaf_node_data(node, begin, end, triangle_indices);
        return node_index;
    }

    // 자식 node들이 사용할 triangle을 나눔
    const std::size_t middle = partition_triangle_items(begin, end, node.max_bounds - node.min_bounds);

    // 재귀적으로 자식 node build
    const std::uint32_t left_child = build_bvh_tree(begin, middle, triangle_indices);
    const std::uint32_t right_child = build_bvh_tree(middle, end, triangle_indices);
    build_nodes_[node_index].left_child = left_child;
    build_nodes_[node_index].right_child = right_child;

    return node_index;
}

std::size_t MeshBvhBuilder::partition_triangle_items(std::size_t begin, std::size_t end, const glm::vec3& extent)
{
    const std::uint32_t axis = find_longest_axis(extent);
    const std::size_t middle = begin + (end - begin) / 2u;

    std::nth_element(triangle_items_.begin() + static_cast<std::ptrdiff_t>(begin),
                     triangle_items_.begin() + static_cast<std::ptrdiff_t>(middle),
                     triangle_items_.begin() + static_cast<std::ptrdiff_t>(end),
                     [axis](const TriangleBuildItem& lhs, const TriangleBuildItem& rhs) {
                         return lhs.center[axis] < rhs.center[axis];
                     });

    return middle;
}

void MeshBvhBuilder::write_leaf_node_data(BvhBuildNode& node, std::size_t begin, std::size_t end, std::vector<std::uint32_t>& triangle_indices) const
{
    node.first_triangle = static_cast<std::uint32_t>(triangle_indices.size() / triangle_vertex_count);
    node.triangle_count = static_cast<std::uint32_t>(end - begin);

    for (std::size_t item_index = begin; item_index < end; ++item_index) {
        const std::size_t index_base = static_cast<std::size_t>(triangle_items_[item_index].triangle_index) * triangle_vertex_count;
        triangle_indices.push_back(source_triangle_indices_[index_base]);
        triangle_indices.push_back(source_triangle_indices_[index_base + 1u]);
        triangle_indices.push_back(source_triangle_indices_[index_base + 2u]);
    }
}

void MeshBvhBuilder::compute_node_bounds(std::size_t begin, std::size_t end, glm::vec3& min_bounds, glm::vec3& max_bounds) const
{
    min_bounds = glm::vec3(std::numeric_limits<float>::max());
    max_bounds = glm::vec3(std::numeric_limits<float>::lowest());

    for (std::size_t item_index = begin; item_index < end; ++item_index) {
        min_bounds = glm::min(min_bounds, triangle_items_[item_index].min_bounds);
        max_bounds = glm::max(max_bounds, triangle_items_[item_index].max_bounds);
    }
}

// dfs 순서로 build된 bvh tree node를 bfs 순서로 바꿔서 전달 //
void MeshBvhBuilder::write_level_ordered_bvh_data(std::uint32_t root_node_index, MeshBvhData& result) const
{
    std::vector<BvhNodeRange> level_order_node_ranges;
    level_order_node_ranges.reserve(build_nodes_.size());
    result.nodes.clear();
    result.nodes.reserve(build_nodes_.size());
    std::vector<std::uint32_t> result_node_indices(build_nodes_.size(), invalid_mesh_bvh_node);

    std::vector<std::uint32_t> current_level_node_indices{root_node_index};

    // root node부터 level을 하나씩 내려가며, node를 level order로 result에 저장 
    while (!current_level_node_indices.empty()) {
        const BvhNodeRange level_range{
            static_cast<std::uint32_t>(result.nodes.size()),
            static_cast<std::uint32_t>(current_level_node_indices.size())
        };
        level_order_node_ranges.push_back(level_range);
        NextBvhLevel next_level(level_range);

        // 현재 level의 node를 탐색하며 result node에 값 저장
        for (const std::uint32_t build_node_index : current_level_node_indices) {
            append_bvh_node(result.nodes, build_node_index, next_level, result_node_indices);
        }

        current_level_node_indices = std::move(next_level.node_indices);
    }

    write_node_metadata(root_node_index, invalid_mesh_bvh_node, result.nodes, result_node_indices);

    // shader update 순서에 맞게 leaf->root 순서로 반전해서 저장
    result.node_ranges_by_level.clear();
    result.node_ranges_by_level.reserve(level_order_node_ranges.size());
    for (auto range = level_order_node_ranges.rbegin(); range != level_order_node_ranges.rend(); ++range) {
        result.node_ranges_by_level.push_back(*range);
    }
}

void MeshBvhBuilder::append_bvh_node(std::vector<MeshBvhNode>& result_nodes,
                                     std::uint32_t build_node_index,
                                     NextBvhLevel& next_level,
                                     std::vector<std::uint32_t>& result_node_indices) const
{
    const BvhBuildNode& build_node = build_nodes_[build_node_index];
    const std::uint32_t result_node_index = static_cast<std::uint32_t>(result_nodes.size());
    MeshBvhNode& node = result_nodes.emplace_back();
    result_node_indices[build_node_index] = result_node_index;
    node.min_bounds = glm::vec4(build_node.min_bounds, 0.0f);
    node.max_bounds = glm::vec4(build_node.max_bounds, 0.0f);

    // metadata에 node 정보 저장
    if (build_node.triangle_count > 0) {
        // leaf node인 경우, triangle 정보 저장
        node.metadata = glm::uvec4(
            invalid_mesh_bvh_node,
            invalid_mesh_bvh_node,
            build_node.first_triangle,
            build_node.triangle_count
        );
    } else {
        // internal node인 경우, 자식 node index 저장
        const auto left_node_index =
            next_level.first_node + static_cast<std::uint32_t>(next_level.node_indices.size());
        next_level.node_indices.push_back(build_node.left_child);

        const auto right_node_index =
            next_level.first_node + static_cast<std::uint32_t>(next_level.node_indices.size());
        next_level.node_indices.push_back(build_node.right_child);

        node.metadata = glm::uvec4(left_node_index, right_node_index, 0u, 0u);
    }
}

void MeshBvhBuilder::write_node_metadata(
    std::uint32_t build_node_index,
    std::uint32_t next_build_node_index,
    std::vector<MeshBvhNode>& result_nodes,
    const std::vector<std::uint32_t>& result_node_indices) const
{
    const std::uint32_t result_node_index = result_node_indices[build_node_index];
    const std::uint32_t next_node_index = (next_build_node_index == invalid_mesh_bvh_node)
        ? invalid_mesh_bvh_node
        : result_node_indices[next_build_node_index];
    const BvhBuildNode& build_node = build_nodes_[build_node_index];

    if (build_node.triangle_count > 0) {
        result_nodes[result_node_index].metadata = glm::uvec4(
            build_node.first_triangle,
            build_node.triangle_count,
            next_node_index,
            1u
        );
        return;
    }

    const std::uint32_t left_node_index = result_node_indices[build_node.left_child];
    const std::uint32_t right_node_index = result_node_indices[build_node.right_child];
    result_nodes[result_node_index].metadata = glm::uvec4(left_node_index, right_node_index, next_node_index, 0u);

    write_node_metadata(build_node.left_child, build_node.right_child, result_nodes, result_node_indices);
    write_node_metadata(build_node.right_child, next_build_node_index, result_nodes, result_node_indices);
}

bool MeshBvhData::is_valid(std::uint32_t triangle_count) const
{
    return triangle_count > 0 &&
           root_node_index < nodes.size() &&
           !nodes.empty() &&
           !node_ranges_by_level.empty() &&
           triangle_indices.size() == static_cast<std::size_t>(triangle_count) * triangle_vertex_count;
}
