#include "gpu/scene/GarmentAttachmentTargetBuilder.h"

#include "asset/MeshGeometryUtils.h"

#include <iostream>
#include <limits>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace {
constexpr float attachment_surface_offset = 0.01f;
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
    targets.reserve(character_triangle_indices.size() / 3u);

    for (std::uint32_t triangle_index = 0;
         triangle_index < static_cast<std::uint32_t>(character_triangle_indices.size() / 3u);
         ++triangle_index) {
        const std::size_t index_base = static_cast<std::size_t>(triangle_index) * 3u;
        const std::uint32_t vertex_a = character_triangle_indices[index_base];
        const std::uint32_t vertex_b = character_triangle_indices[index_base + 1u];
        const std::uint32_t vertex_c = character_triangle_indices[index_base + 2u];
        if (vertex_a >= character_mesh.vertex_count ||
            vertex_b >= character_mesh.vertex_count ||
            vertex_c >= character_mesh.vertex_count) {
            continue;
        }

        targets.push_back({
            get_character_position(character_mesh, character_frame_index, vertex_a),
            get_character_position(character_mesh, character_frame_index, vertex_b),
            get_character_position(character_mesh, character_frame_index, vertex_c),
            triangle_index
        });
    }

    return targets;
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
}

namespace garment_attachment_target_builder {

std::vector<GarmentAttachmentConstraint> build_garment_attachment_targets(
    const GarmentObject& garment,
    const CharacterMesh& character_mesh,
    std::uint32_t character_frame_index,
    const std::vector<std::uint32_t>& character_triangle_indices)
{
    std::vector<GarmentAttachmentConstraint> targets;
    targets.reserve(garment.mesh.attachment_vertex_indices.size());

    if (garment.mesh.attachment_vertex_indices.empty() ||
        character_mesh.vertex_count == 0u ||
        character_triangle_indices.empty() ||
        character_triangle_indices.size() % 3u != 0u) {
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
        ClosestTrianglePoint best_point;
        std::uint32_t best_triangle_index = 0;
        for (const CharacterTriangleTarget& triangle : character_triangles) {
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
