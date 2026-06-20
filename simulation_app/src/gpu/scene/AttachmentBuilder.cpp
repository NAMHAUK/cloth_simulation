#include "gpu/scene/AttachmentBuilder.h"

#include "asset/MeshGeometryUtils.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace {
constexpr float attachment_surface_offset = 0.005f;
constexpr float degenerate_triangle_epsilon = 1.0e-12f;

struct ClosestTrianglePoint final {
    glm::vec3 point{};
    glm::vec3 barycentric{};
    float distance_sq = std::numeric_limits<float>::max();
    bool valid = false;
};

struct CharacterTriangleTarget final {
    glm::vec3 a{};
    glm::vec3 b{};
    glm::vec3 c{};
    std::uint32_t triangle_index = 0;
};

struct BvhNodeVisit final {
    std::uint32_t node_index = 0;
    float distance_sq = 0.0f;
};

bool is_leaf_node(const MeshBvhNode& node)
{
    return node.triangle_count > 0u;
}

ClosestTrianglePoint make_valid_closest_point(const glm::vec3& query,
                                              const glm::vec3& closest_point,
                                              const glm::vec3& barycentric)
{
    return {
        closest_point,
        barycentric,
        glm::dot(query - closest_point, query - closest_point),
        true
    };
}

glm::vec3 get_character_position(const CharacterMesh& character_mesh,
                                 std::uint32_t frame_index,
                                 std::uint32_t vertex_index)
{
    const std::size_t position_base =
        (static_cast<std::size_t>(frame_index) * character_mesh.vertex_count + vertex_index) * 3u;
    return {
        character_mesh.vertices[position_base],
        character_mesh.vertices[position_base + 1u],
        character_mesh.vertices[position_base + 2u],
    };
}

std::vector<CharacterTriangleTarget> build_character_triangle_targets(
    const CharacterMesh& character_mesh,
    std::uint32_t character_frame_index,
    const std::vector<std::uint32_t>& character_triangle_indices)
{
    std::vector<CharacterTriangleTarget> targets;
    const auto triangle_count = static_cast<std::uint32_t>(character_triangle_indices.size() / 3u);
    targets.reserve(triangle_count);

    for (std::uint32_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        const std::size_t index_base = static_cast<std::size_t>(triangle_index) * 3u;
        const std::uint32_t vertex_a = character_triangle_indices[index_base];
        const std::uint32_t vertex_b = character_triangle_indices[index_base + 1u];
        const std::uint32_t vertex_c = character_triangle_indices[index_base + 2u];

        targets.push_back({
            get_character_position(character_mesh, character_frame_index, vertex_a),
            get_character_position(character_mesh, character_frame_index, vertex_b),
            get_character_position(character_mesh, character_frame_index, vertex_c),
            triangle_index
        });
    }

    return targets;
}

float squared_distance_to_bounds(const glm::vec3& point, const glm::vec3& min_bounds, const glm::vec3& max_bounds)
{
    const glm::vec3 clamped_point = glm::clamp(point, min_bounds, max_bounds);
    return glm::dot(point - clamped_point, point - clamped_point);
}

ClosestTrianglePoint closest_point_on_triangle(const glm::vec3& point,
                                               const glm::vec3& a,
                                               const glm::vec3& b,
                                               const glm::vec3& c)
{
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 face_normal = glm::cross(ab, ac);
    if (glm::dot(face_normal, face_normal) <= degenerate_triangle_epsilon) {
        return {};
    }

    const glm::vec3 ap = point - a;
    const float d1 = glm::dot(ab, ap);
    const float d2 = glm::dot(ac, ap);

    // Vertex region A
    if (d1 <= 0.0f && d2 <= 0.0f) {
        return make_valid_closest_point(point, a, {1.0f, 0.0f, 0.0f});
    }

    const glm::vec3 bp = point - b;
    const float d3 = glm::dot(ab, bp);
    const float d4 = glm::dot(ac, bp);

    // Vertex region B
    if (d3 >= 0.0f && d4 <= d3) {
        return make_valid_closest_point(point, b, {0.0f, 1.0f, 0.0f});
    }

    // Edge region AB
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        return make_valid_closest_point(point, a + v * ab, {1.0f - v, v, 0.0f});
    }

    const glm::vec3 cp = point - c;
    const float d5 = glm::dot(ab, cp);
    const float d6 = glm::dot(ac, cp);

    // Vertex region C
    if (d6 >= 0.0f && d5 <= d6) {
        return make_valid_closest_point(point, c, {0.0f, 0.0f, 1.0f});
    }

    // Edge region AC
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        return make_valid_closest_point(point, a + w * ac, {1.0f - w, 0.0f, w});
    }

    // Edge region BC
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return make_valid_closest_point(point, b + w * (c - b), {0.0f, 1.0f - w, w});
    }

    // Face region
    const float denom = 1.0f / (va + vb + vc);
    const float v = vb * denom;
    const float w = vc * denom;
    return make_valid_closest_point(point, a + ab * v + ac * w, {1.0f - v - w, v, w});
}

ClosestTrianglePoint find_closest_triangle_with_bvh(const glm::vec3& cloth_position,
                                                    const std::vector<CharacterTriangleTarget>& character_triangles,
                                                    const std::vector<MeshBvhNode>& bvh_nodes,
                                                    std::uint32_t& best_triangle_index)
{
    ClosestTrianglePoint best_point;
    std::vector<BvhNodeVisit> node_stack;
    node_stack.reserve(64);
    if (!bvh_nodes.empty()) {
        node_stack.push_back({
            0u,
            squared_distance_to_bounds(cloth_position, bvh_nodes[0].min_bounds, bvh_nodes[0].max_bounds)
        });
    }

    while (!node_stack.empty()) {
        const BvhNodeVisit visit = node_stack.back();
        node_stack.pop_back();
        if (visit.node_index >= bvh_nodes.size() || visit.distance_sq > best_point.distance_sq) {
            continue;
        }

        const MeshBvhNode& node = bvh_nodes[visit.node_index];
        if (is_leaf_node(node)) {
            const std::uint32_t first_triangle = node.first_triangle;
            const std::uint32_t triangle_count = node.triangle_count;
            for (std::uint32_t triangle_offset = 0; triangle_offset < triangle_count; ++triangle_offset) {
                const std::uint32_t triangle_index = first_triangle + triangle_offset;
                if (triangle_index >= character_triangles.size()) {
                    continue;
                }

                const CharacterTriangleTarget& triangle = character_triangles[triangle_index];
                const ClosestTrianglePoint candidate = closest_point_on_triangle(
                    cloth_position,
                    triangle.a,
                    triangle.b,
                    triangle.c
                );
                if (candidate.valid && candidate.distance_sq < best_point.distance_sq) {
                    best_point = candidate;
                    best_triangle_index = triangle.triangle_index;
                }
            }
            continue;
        }

        const std::uint32_t left_child = node.left_child;
        const std::uint32_t right_child = node.right_child;
        const bool has_left_child = left_child < bvh_nodes.size();
        const bool has_right_child = right_child < bvh_nodes.size();
        if (has_left_child && has_right_child) {
            const float left_distance_sq = squared_distance_to_bounds(
                cloth_position,
                bvh_nodes[left_child].min_bounds,
                bvh_nodes[left_child].max_bounds
            );
            const float right_distance_sq = squared_distance_to_bounds(
                cloth_position,
                bvh_nodes[right_child].min_bounds,
                bvh_nodes[right_child].max_bounds
            );
            const std::uint32_t near_child = left_distance_sq <= right_distance_sq ? left_child : right_child;
            const std::uint32_t far_child = left_distance_sq <= right_distance_sq ? right_child : left_child;
            const float near_distance_sq = std::min(left_distance_sq, right_distance_sq);
            const float far_distance_sq = std::max(left_distance_sq, right_distance_sq);

            if (far_distance_sq <= best_point.distance_sq) {
                node_stack.push_back({far_child, far_distance_sq});
            }
            if (near_distance_sq <= best_point.distance_sq) {
                node_stack.push_back({near_child, near_distance_sq});
            }
        } else if (has_left_child || has_right_child) {
            const std::uint32_t child = has_left_child ? left_child : right_child;
            const float child_distance_sq = squared_distance_to_bounds(
                cloth_position,
                bvh_nodes[child].min_bounds,
                bvh_nodes[child].max_bounds
            );
            if (child_distance_sq <= best_point.distance_sq) {
                node_stack.push_back({child, child_distance_sq});
            }
        }
    }

    return best_point;
}
}

namespace attachment_builder {

std::vector<GarmentAttachmentConstraint> build_garment_attachment_targets(
    const GarmentObject& garment,
    const CharacterMesh& character_mesh,
    std::uint32_t character_frame_index,
    const std::vector<std::uint32_t>& character_triangle_indices,
    const std::vector<MeshBvhNode>& character_bvh_nodes)
{
    std::vector<GarmentAttachmentConstraint> targets;
    targets.reserve(garment.mesh.attachment_vertex_indices.size());

    if (garment.mesh.attachment_vertex_indices.empty() ||
        character_mesh.vertex_count == 0u ||
        character_triangle_indices.empty() ||
        character_triangle_indices.size() % 3u != 0u ||
        character_bvh_nodes.empty()) {
        return targets;
    }

    const auto garment_vertex_count = static_cast<std::uint32_t>(garment.mesh.vertices.size() / 3u);
    const std::vector<CharacterTriangleTarget> character_triangles = build_character_triangle_targets(
        character_mesh,
        character_frame_index,
        character_triangle_indices
    );
    if (character_triangles.empty()) {
        return targets;
    }

    for (std::uint32_t cloth_vertex_index : garment.mesh.attachment_vertex_indices) {
        if (cloth_vertex_index >= garment_vertex_count) {
            std::cerr << "Skipping invalid garment attachment vertex index.\n";
            continue;
        }

        const glm::vec3 cloth_position = get_vertex_position(garment.mesh.vertices, cloth_vertex_index);
        std::uint32_t best_triangle_index = 0;
        const ClosestTrianglePoint best_point = find_closest_triangle_with_bvh(
            cloth_position,
            character_triangles,
            character_bvh_nodes,
            best_triangle_index
        );

        if (!best_point.valid) {
            std::cerr << "Skipping garment attachment vertex without a valid character target.\n";
            continue;
        }

        targets.push_back({
            cloth_vertex_index,
            best_triangle_index,
            glm::vec4(best_point.barycentric, attachment_surface_offset)
        });
    }

    return targets;
}

}
