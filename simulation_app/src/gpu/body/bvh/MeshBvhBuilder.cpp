#include "gpu/body/bvh/MeshBvhBuilder.h"

#include "asset/MeshGeometryUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace {
constexpr std::uint32_t triangle_vertex_count = 3;
constexpr std::uint32_t mesh_bvh_leaf_size = 8;
constexpr std::size_t shader_max_bvh_stack_depth = 32u;
constexpr std::size_t character_part_label_count = 6u;

struct PartLabelStats final {
    glm::vec3 min_bounds{std::numeric_limits<float>::max()};
    glm::vec3 max_bounds{std::numeric_limits<float>::lowest()};
    std::uint32_t triangle_count = 0;
};

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

bool has_multiple_bits(std::uint32_t mask)
{
    return (mask & (mask - 1u)) != 0u;
}

double surface_area(const glm::vec3& min_bounds, const glm::vec3& max_bounds)
{
    const glm::vec3 extent = glm::max(max_bounds - min_bounds, glm::vec3{0.0f});
    return 2.0 * (static_cast<double>(extent.x) * extent.y +
                  static_cast<double>(extent.y) * extent.z +
                  static_cast<double>(extent.z) * extent.x);
}

bool is_leaf_node(const MeshBvhNode& node)
{
    return node.triangle_count > 0u;
}

bool is_valid_leaf_node(const MeshBvhNode& node, std::uint32_t source_triangle_count)
{
    return node.triangle_count > 0u &&
           node.left_child_index == invalid_mesh_bvh_node &&
           node.right_child_index == invalid_mesh_bvh_node &&
           node.first_triangle_index <= source_triangle_count &&
           node.triangle_count <= source_triangle_count - node.first_triangle_index;
}

bool is_valid_internal_node(const MeshBvhNode& node, std::size_t node_index, std::size_t node_count)
{
    return node.triangle_count == 0u &&
           node.left_child_index > node_index &&
           node.right_child_index > node_index &&
           node.left_child_index != node.right_child_index &&
           node.left_child_index < node_count &&
           node.right_child_index < node_count;
}

bool has_valid_shader_stack_depth(const MeshBvhData& bvh)
{
    std::vector<std::uint32_t> node_stack;
    node_stack.reserve(shader_max_bvh_stack_depth);
    node_stack.push_back(bvh.root_node_index);

    while (!node_stack.empty()) {
        if (node_stack.size() > shader_max_bvh_stack_depth) {
            return false;
        }

        const std::uint32_t node_index = node_stack.back();
        node_stack.pop_back();

        const MeshBvhNode& node = bvh.nodes[node_index];
        if (is_leaf_node(node)) {
            continue;
        }

        node_stack.push_back(node.right_child_index);
        node_stack.push_back(node.left_child_index);
    }

    return true;
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

MeshBvhBuilder::MeshBvhBuilder(std::uint32_t vertex_count,
                               const std::vector<std::uint32_t>& triangle_indices,
                               const std::vector<float>& vertices,
                               const std::vector<std::uint8_t>& triangle_part_labels)
    : vertex_count_(vertex_count),
      source_triangle_indices_(triangle_indices),
      vertices_(vertices),
      triangle_part_labels_(&triangle_part_labels)
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
    if (has_part_labels() && triangle_part_labels_->size() != triangle_count) {
        return false;
    }

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
            max_bounds,
            has_part_labels() ? (*triangle_part_labels_)[triangle_index] : 0u
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
    if (has_part_labels()) {
        const std::uint32_t part_label_mask = compute_part_label_mask(begin, end);
        if (has_multiple_bits(part_label_mask)) {
            const std::uint32_t left_part_label_mask = find_best_part_label_split_mask(begin, end, part_label_mask);
            const std::size_t middle = partition_triangle_items_by_part_labels(begin, end, left_part_label_mask);

            const std::uint32_t left_child_index = build_bvh_tree(begin, middle, triangle_indices);
            const std::uint32_t right_child_index = build_bvh_tree(middle, end, triangle_indices);
            build_nodes_[node_index].left_child_index = left_child_index;
            build_nodes_[node_index].right_child_index = right_child_index;

            return node_index;
        }
    }
    if (triangle_count <= mesh_bvh_leaf_size) {
        write_leaf_node_data(node, begin, end, triangle_indices);
        return node_index;
    }

    // 자식 node들이 사용할 triangle을 나눔
    const std::size_t middle = partition_triangle_items(begin, end, node.max_bounds - node.min_bounds);

    // 재귀적으로 자식 node build
    const std::uint32_t left_child_index = build_bvh_tree(begin, middle, triangle_indices);
    const std::uint32_t right_child_index = build_bvh_tree(middle, end, triangle_indices);
    build_nodes_[node_index].left_child_index = left_child_index;
    build_nodes_[node_index].right_child_index = right_child_index;

    return node_index;
}

bool MeshBvhBuilder::has_part_labels() const
{
    return triangle_part_labels_ != nullptr && !triangle_part_labels_->empty();
}

std::uint32_t MeshBvhBuilder::compute_part_label_mask(std::size_t begin, std::size_t end) const
{
    std::uint32_t part_label_mask = 0u;
    for (std::size_t item_index = begin; item_index < end; ++item_index) {
        part_label_mask |= 1u << triangle_items_[item_index].part_label;
    }
    return part_label_mask;
}

std::uint32_t MeshBvhBuilder::find_best_part_label_split_mask(std::size_t begin,
                                                              std::size_t end,
                                                              std::uint32_t part_label_mask) const
{
    std::array<PartLabelStats, character_part_label_count> stats_by_label;
    for (std::size_t item_index = begin; item_index < end; ++item_index) {
        const TriangleBuildItem& item = triangle_items_[item_index];
        PartLabelStats& stats = stats_by_label[item.part_label];
        stats.min_bounds = glm::min(stats.min_bounds, item.min_bounds);
        stats.max_bounds = glm::max(stats.max_bounds, item.max_bounds);
        ++stats.triangle_count;
    }

    double best_cost = std::numeric_limits<double>::max();
    std::uint32_t best_split_mask = 0u;
    const std::uint32_t anchor_label_mask = part_label_mask & (~part_label_mask + 1u);

    for (std::uint32_t split_mask = (part_label_mask - 1u) & part_label_mask;
         split_mask != 0u;
         split_mask = (split_mask - 1u) & part_label_mask) {
        if ((split_mask & anchor_label_mask) == 0u) {
            continue;
        }

        PartLabelStats left_stats;
        PartLabelStats right_stats;
        const std::uint32_t right_split_mask = part_label_mask ^ split_mask;
        for (std::size_t label = 0; label < stats_by_label.size(); ++label) {
            const std::uint32_t label_mask = 1u << label;
            const PartLabelStats& source_stats = stats_by_label[label];
            if (source_stats.triangle_count == 0u) {
                continue;
            }

            PartLabelStats& target_stats = (split_mask & label_mask) != 0u ? left_stats : right_stats;
            target_stats.min_bounds = glm::min(target_stats.min_bounds, source_stats.min_bounds);
            target_stats.max_bounds = glm::max(target_stats.max_bounds, source_stats.max_bounds);
            target_stats.triangle_count += source_stats.triangle_count;
        }

        if (left_stats.triangle_count == 0u || right_stats.triangle_count == 0u || right_split_mask == 0u) {
            continue;
        }

        const double cost =
            surface_area(left_stats.min_bounds, left_stats.max_bounds) * left_stats.triangle_count +
            surface_area(right_stats.min_bounds, right_stats.max_bounds) * right_stats.triangle_count;
        if (cost < best_cost) {
            best_cost = cost;
            best_split_mask = split_mask;
        }
    }

    return best_split_mask;
}

std::size_t MeshBvhBuilder::partition_triangle_items_by_part_labels(std::size_t begin,
                                                                    std::size_t end,
                                                                    std::uint32_t left_part_label_mask)
{
    const auto middle = std::partition(triangle_items_.begin() + static_cast<std::ptrdiff_t>(begin),
                                       triangle_items_.begin() + static_cast<std::ptrdiff_t>(end),
                                       [left_part_label_mask](const TriangleBuildItem& item) {
                                           return (left_part_label_mask & (1u << item.part_label)) != 0u;
                                       });
    return static_cast<std::size_t>(middle - triangle_items_.begin());
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
    node.first_triangle_index = static_cast<std::uint32_t>(triangle_indices.size() / triangle_vertex_count);
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
            append_bvh_node(result.nodes, build_node_index, next_level);
        }

        current_level_node_indices = std::move(next_level.node_indices);
    }


    // shader update 순서에 맞게 leaf->root 순서로 반전해서 저장
    result.node_ranges_by_level.clear();
    result.node_ranges_by_level.reserve(level_order_node_ranges.size());
    for (auto range = level_order_node_ranges.rbegin(); range != level_order_node_ranges.rend(); ++range) {
        result.node_ranges_by_level.push_back(*range);
    }
}

void MeshBvhBuilder::append_bvh_node(std::vector<MeshBvhNode>& result_nodes,
                                     std::uint32_t build_node_index,
                                     NextBvhLevel& next_level) const
{
    const BvhBuildNode& build_node = build_nodes_[build_node_index];
    MeshBvhNode& node = result_nodes.emplace_back();
    node.min_bounds = glm::vec4(build_node.min_bounds, 0.0f);
    node.max_bounds = glm::vec4(build_node.max_bounds, 0.0f);

    // node reference 정보 저장
    if (build_node.triangle_count > 0) {
        // leaf node인 경우, triangle 정보 저장
        node.first_triangle_index = build_node.first_triangle_index;
        node.triangle_count = build_node.triangle_count;
    } else {
        // internal node인 경우, 자식 node index 저장
        const auto left_node_index =
            next_level.first_node + static_cast<std::uint32_t>(next_level.node_indices.size());
        next_level.node_indices.push_back(build_node.left_child_index);

        const auto right_node_index =
            next_level.first_node + static_cast<std::uint32_t>(next_level.node_indices.size());
        next_level.node_indices.push_back(build_node.right_child_index);

        node.left_child_index = left_node_index;
        node.right_child_index = right_node_index;
    }
}

bool MeshBvhData::is_valid(std::uint32_t triangle_count) const
{
    if (triangle_count == 0 ||
        nodes.empty() ||
        node_ranges_by_level.empty() ||
        root_node_index >= nodes.size() ||
        triangle_indices.size() != static_cast<std::size_t>(triangle_count) * triangle_vertex_count) {
        return false;
    }

    const std::size_t node_count = nodes.size();
    for (std::size_t node_index = 0; node_index < node_count; ++node_index) {
        const MeshBvhNode& node = nodes[node_index];
        if (is_leaf_node(node)) {
            if (!is_valid_leaf_node(node, triangle_count)) {
                return false;
            }
            continue;
        }

        if (!is_valid_internal_node(node, node_index, node_count)) {
            return false;
        }
    }

    return has_valid_shader_stack_depth(*this);
}
