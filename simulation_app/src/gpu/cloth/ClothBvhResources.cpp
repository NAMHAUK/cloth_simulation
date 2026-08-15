#include "gpu/cloth/ClothBvhResources.h"

#include "scene/SceneState.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {
constexpr std::uint32_t triangle_vertex_count = 3u;

struct PackedClothBvhData final
{
    std::vector<std::uint32_t> triangle_indices;
    std::vector<BvhNode> nodes;
    std::array<GarmentBvhRanges, 2> garment_ranges;
    std::uint32_t triangle_count = 0;
    std::uint32_t node_count = 0;
    std::uint32_t garment_count = 0;
};

PackedClothBvhData build_packed_data(const std::vector<GarmentObject>& garments)
{
    PackedClothBvhData packed_data;

    for (const GarmentObject& garment : garments) {
        const TriangleBvhData& bvh = garment.triangle_bvh;
        const std::size_t triangle_count = bvh.triangle_indices.size() / triangle_vertex_count;
        const auto local_triangle_count = static_cast<std::uint32_t>(triangle_count);

        GarmentBvhRanges& ranges = packed_data.garment_ranges[garment.layer];
        ranges.collision_triangles = {packed_data.triangle_count, local_triangle_count};
        ranges.nodes = {packed_data.node_count, static_cast<std::uint32_t>(bvh.nodes.size())};
        ranges.node_ranges_by_level = bvh.node_ranges_by_level;

        packed_data.triangle_indices.insert(packed_data.triangle_indices.end(),
                                            bvh.triangle_indices.begin(),
                                            bvh.triangle_indices.end());
        packed_data.nodes.insert(packed_data.nodes.end(), bvh.nodes.begin(), bvh.nodes.end());
        packed_data.triangle_count += local_triangle_count;
        packed_data.node_count += static_cast<std::uint32_t>(bvh.nodes.size());
    }
    packed_data.garment_count = static_cast<std::uint32_t>(garments.size());

    return packed_data;
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
    return {collision_triangle_index_,
            bvh_node_,
            triangle_bounds_,
            triangle_count_,
            node_count_,
            garment_count_,
            &garment_ranges_};
}

void ClothBvhResources::rebuild(const std::vector<GarmentObject>& garments, QOpenGLFunctions_4_5_Core& gl)
{
    PackedClothBvhData packed_data = build_packed_data(garments);

    GLuint next_collision_triangle_index = 0;
    GLuint next_bvh_node = 0;
    GLuint next_triangle_bounds = 0;
    gl.glCreateBuffers(1, &next_collision_triangle_index);
    gl.glCreateBuffers(1, &next_bvh_node);
    gl.glCreateBuffers(1, &next_triangle_bounds);

    const GLsizeiptr triangle_index_bytes =
        static_cast<GLsizeiptr>(packed_data.triangle_indices.size() * sizeof(std::uint32_t));
    const GLsizeiptr bvh_node_bytes = static_cast<GLsizeiptr>(packed_data.nodes.size() * sizeof(BvhNode));
    const GLsizeiptr triangle_bounds_bytes =
        static_cast<GLsizeiptr>(static_cast<std::size_t>(packed_data.triangle_count) * sizeof(Aabb));
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
    garment_ranges_ = std::move(packed_data.garment_ranges);
    triangle_count_ = packed_data.triangle_count;
    node_count_ = packed_data.node_count;
    garment_count_ = packed_data.garment_count;
}

void ClothBvhResources::release(QOpenGLFunctions_4_5_Core& gl)
{
    delete_buffers(collision_triangle_index_, bvh_node_, triangle_bounds_, gl);
    garment_ranges_ = {};
    triangle_count_ = 0;
    node_count_ = 0;
    garment_count_ = 0;
}
