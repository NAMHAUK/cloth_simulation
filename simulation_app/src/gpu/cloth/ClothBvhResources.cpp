#include "gpu/cloth/ClothBvhResources.h"

#include "scene/SceneState.h"
#include "utils/BufferUtils.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {
struct ClothBvhData final
{
    std::vector<std::uint32_t> triangle_indices;
    std::vector<BvhNode> nodes;
    std::array<GarmentBvhRanges, 2> garment_ranges;
    std::uint32_t triangle_count = 0;
    std::uint32_t node_count = 0;
};

ClothBvhData build_cloth_bvh_data(const std::vector<GarmentObject>& garments)
{
    ClothBvhData bvh_data;

    for (const GarmentObject& garment : garments) {
        const TriangleBvhData& bvh = garment.triangle_bvh;

        GarmentBvhRanges& ranges = bvh_data.garment_ranges[garment.layer];
        ranges.collision_triangles = {bvh_data.triangle_count, bvh.collision_triangle_count};
        ranges.nodes = {bvh_data.node_count, static_cast<std::uint32_t>(bvh.nodes.size())};
        ranges.node_ranges_by_level = bvh.node_ranges_by_level;

        bvh_data.triangle_indices.insert(bvh_data.triangle_indices.end(),
                                         bvh.triangle_indices.begin(),
                                         bvh.triangle_indices.end());
        bvh_data.nodes.insert(bvh_data.nodes.end(), bvh.nodes.begin(), bvh.nodes.end());
        bvh_data.triangle_count += bvh.collision_triangle_count;
        bvh_data.node_count += static_cast<std::uint32_t>(bvh.nodes.size());
    }

    return bvh_data;
}

}

ClothBvhBufferView ClothBvhResources::buffer_view() const
{
    return {collision_triangle_index_buffer_,
            node_buffer_,
            triangle_bounds_buffer_,
            triangle_count_,
            node_count_,
            garment_count_,
            &garment_ranges_};
}

void ClothBvhResources::rebuild(const std::vector<GarmentObject>& garments, QOpenGLFunctions_4_5_Core& gl)
{
    ClothBvhData bvh_data = build_cloth_bvh_data(garments);

    release(gl);
    gl.glCreateBuffers(1, &collision_triangle_index_buffer_);
    gl.glCreateBuffers(1, &node_buffer_);
    gl.glCreateBuffers(1, &triangle_bounds_buffer_);

    const GLsizeiptr triangle_index_bytes = byte_size<std::uint32_t>(bvh_data.triangle_indices.size());
    const GLsizeiptr bvh_node_bytes = byte_size<BvhNode>(bvh_data.nodes.size());
    const GLsizeiptr triangle_bounds_bytes = byte_size<Aabb>(bvh_data.triangle_count);
    gl.glNamedBufferData(collision_triangle_index_buffer_,
                         triangle_index_bytes,
                         bvh_data.triangle_indices.data(),
                         GL_STATIC_DRAW);
    gl.glNamedBufferData(node_buffer_, bvh_node_bytes, bvh_data.nodes.data(), GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(triangle_bounds_buffer_, triangle_bounds_bytes, nullptr, GL_DYNAMIC_DRAW);

    garment_ranges_ = std::move(bvh_data.garment_ranges);
    triangle_count_ = bvh_data.triangle_count;
    node_count_ = bvh_data.node_count;
    garment_count_ = static_cast<std::uint32_t>(garments.size());
}

void ClothBvhResources::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &triangle_bounds_buffer_);
    gl.glDeleteBuffers(1, &node_buffer_);
    gl.glDeleteBuffers(1, &collision_triangle_index_buffer_);
    collision_triangle_index_buffer_ = 0;
    node_buffer_ = 0;
    triangle_bounds_buffer_ = 0;
    garment_ranges_ = {};
    triangle_count_ = 0;
    node_count_ = 0;
    garment_count_ = 0;
}
