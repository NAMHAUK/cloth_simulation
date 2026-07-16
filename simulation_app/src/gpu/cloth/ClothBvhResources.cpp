#include "gpu/cloth/ClothBvhResources.h"

#include "scene/SceneState.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace {
constexpr std::uint32_t triangle_vertex_count = 3u;

struct PackedClothBvhData final {
    std::vector<std::uint32_t> triangle_indices;
    std::vector<BvhNode> nodes;
    std::vector<GarmentBvhLayout> garment_layouts;
    std::uint32_t triangle_count = 0;
    std::uint32_t node_count = 0;
};

bool can_append(std::uint32_t current_count, std::size_t appended_count)
{
    return appended_count <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() - current_count);
}

bool build_packed_data(const std::vector<GarmentObject>& garments, PackedClothBvhData& packed_data)
{
    packed_data.garment_layouts.reserve(garments.size());

    for (const GarmentObject& garment : garments) {
        if (!garment.garment_triangle_bvh.has_value()) {
            return false;
        }

        const TriangleBvhData& bvh = *garment.garment_triangle_bvh;
        const std::size_t triangle_count = bvh.triangle_indices.size() / triangle_vertex_count;
        if (!can_append(packed_data.triangle_count, triangle_count) ||
            !can_append(packed_data.node_count, bvh.nodes.size())) {
            return false;
        }

        const auto local_triangle_count = static_cast<std::uint32_t>(triangle_count);
        if (!bvh.is_valid(local_triangle_count)) {
            return false;
        }

        GarmentBvhLayout layout;
        layout.range.garment_id = garment.id;
        layout.range.layer = garment.layer;
        layout.range.collision_triangles = {packed_data.triangle_count, local_triangle_count};
        layout.range.bvh_nodes = {packed_data.node_count, static_cast<std::uint32_t>(bvh.nodes.size())};
        layout.node_ranges_by_level = bvh.node_ranges_by_level;

        packed_data.triangle_indices.insert(packed_data.triangle_indices.end(),
                                            bvh.triangle_indices.begin(),
                                            bvh.triangle_indices.end());
        packed_data.nodes.insert(packed_data.nodes.end(), bvh.nodes.begin(), bvh.nodes.end());
        packed_data.garment_layouts.push_back(std::move(layout));
        packed_data.triangle_count += local_triangle_count;
        packed_data.node_count += static_cast<std::uint32_t>(bvh.nodes.size());
    }

    return !packed_data.garment_layouts.empty();
}

void delete_buffers(GLuint& collision_triangle_index,
                    GLuint& bvh_node,
                    GLuint& triangle_bounds,
                    QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &triangle_bounds);
    gl.glDeleteBuffers(1, &bvh_node);
    gl.glDeleteBuffers(1, &collision_triangle_index);
    collision_triangle_index = 0;
    bvh_node = 0;
    triangle_bounds = 0;
}
}

ClothBvhBufferView ClothBvhResources::buffer_view() const
{
    return {collision_triangle_index_, bvh_node_, triangle_bounds_, triangle_count_, node_count_, &garment_layouts_};
}

bool ClothBvhResources::rebuild(const std::vector<GarmentObject>& garments, QOpenGLFunctions_4_5_Core& gl)
{
    if (garments.empty()) {
        release(gl);
        return true;
    }

    PackedClothBvhData packed_data;
    if (!build_packed_data(garments, packed_data)) {
        release(gl);
        return false;
    }

    GLuint next_collision_triangle_index = 0;
    GLuint next_bvh_node = 0;
    GLuint next_triangle_bounds = 0;
    gl.glCreateBuffers(1, &next_collision_triangle_index);
    gl.glCreateBuffers(1, &next_bvh_node);
    gl.glCreateBuffers(1, &next_triangle_bounds);
    if (next_collision_triangle_index == 0 || next_bvh_node == 0 || next_triangle_bounds == 0) {
        delete_buffers(next_collision_triangle_index, next_bvh_node, next_triangle_bounds, gl);
        release(gl);
        return false;
    }

    const GLsizeiptr triangle_index_bytes = static_cast<GLsizeiptr>(packed_data.triangle_indices.size() * sizeof(std::uint32_t));
    const GLsizeiptr bvh_node_bytes = static_cast<GLsizeiptr>(packed_data.nodes.size() * sizeof(BvhNode));
    const GLsizeiptr triangle_bounds_bytes = static_cast<GLsizeiptr>(static_cast<std::size_t>(packed_data.triangle_count) * sizeof(Aabb));
    gl.glNamedBufferData(next_collision_triangle_index,
                         triangle_index_bytes,
                         packed_data.triangle_indices.data(),
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(next_bvh_node, bvh_node_bytes, packed_data.nodes.data(), GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(next_triangle_bounds, triangle_bounds_bytes, nullptr, GL_DYNAMIC_DRAW);

    release(gl);
    collision_triangle_index_ = next_collision_triangle_index;
    bvh_node_ = next_bvh_node;
    triangle_bounds_ = next_triangle_bounds;
    garment_layouts_ = std::move(packed_data.garment_layouts);
    triangle_count_ = packed_data.triangle_count;
    node_count_ = packed_data.node_count;
    return true;
}

void ClothBvhResources::release(QOpenGLFunctions_4_5_Core& gl)
{
    delete_buffers(collision_triangle_index_, bvh_node_, triangle_bounds_, gl);
    garment_layouts_.clear();
    triangle_count_ = 0;
    node_count_ = 0;
}
