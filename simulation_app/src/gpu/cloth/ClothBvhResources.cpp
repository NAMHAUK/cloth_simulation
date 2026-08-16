#include "gpu/cloth/ClothBvhResources.h"

#include "gpu/bvh/BvhBuildUtils.h"
#include "scene/SceneState.h"
#include "utils/BufferUtils.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {
struct ClothBvhData final
{
    std::vector<BvhNode> nodes;
    std::array<GarmentBvhRanges, 2> garment_ranges;
    std::uint32_t node_count = 0;
};

void append_bvh_data(const TriangleBvhData& bvh,
                     std::uint32_t triangle_offset,
                     GarmentBvhRanges& ranges,
                     std::vector<BvhNode>& nodes)
{
    // Append garment BVH data and adjust indices to the global range
    for (BvhNodeRange& level_range : ranges.node_ranges_by_level) {
        level_range.first_node += ranges.nodes.offset;
    }
    for (BvhNode node : bvh.nodes) {
        if (bvh_build::is_leaf_node(node.element_count)) {
            node.first_element_index += triangle_offset;
        } else {
            node.left_child_index += ranges.nodes.offset;
            node.right_child_index += ranges.nodes.offset;
        }
        nodes.push_back(node);
    }
}

ClothBvhData build_cloth_bvh_data(const std::vector<GarmentObject>& garments)
{
    ClothBvhData bvh_data;
    std::uint32_t triangle_offset = 0;

    for (const GarmentObject& garment : garments) {
        const TriangleBvhData& bvh = garment.triangle_bvh;

        GarmentBvhRanges& ranges = bvh_data.garment_ranges[garment.layer];
        ranges.nodes = {bvh_data.node_count, static_cast<std::uint32_t>(bvh.nodes.size())};
        ranges.node_ranges_by_level = bvh.node_ranges_by_level;

        append_bvh_data(bvh, triangle_offset, ranges, bvh_data.nodes);
        triangle_offset += bvh.collision_triangle_count;
        bvh_data.node_count += static_cast<std::uint32_t>(bvh.nodes.size());
    }

    return bvh_data;
}
}

ClothBvhBufferView ClothBvhResources::buffer_view() const
{
    return {node_buffer_, triangle_bounds_buffer_, node_count_, garment_count_, &garment_ranges_};
}

void ClothBvhResources::rebuild(const std::vector<GarmentObject>& garments,
                                std::uint32_t triangle_count,
                                QOpenGLFunctions_4_5_Core& gl)
{
    ClothBvhData bvh_data = build_cloth_bvh_data(garments);

    release(gl);
    gl.glCreateBuffers(1, &node_buffer_);
    gl.glCreateBuffers(1, &triangle_bounds_buffer_);

    const GLsizeiptr bvh_node_bytes = byte_size<BvhNode>(bvh_data.nodes.size());
    const GLsizeiptr triangle_bounds_bytes = byte_size<Aabb>(triangle_count);
    gl.glNamedBufferData(node_buffer_, bvh_node_bytes, bvh_data.nodes.data(), GL_DYNAMIC_DRAW);
    gl.glNamedBufferData(triangle_bounds_buffer_, triangle_bounds_bytes, nullptr, GL_DYNAMIC_DRAW);

    garment_ranges_ = std::move(bvh_data.garment_ranges);
    node_count_ = bvh_data.node_count;
    garment_count_ = static_cast<std::uint32_t>(garments.size());
}

void ClothBvhResources::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteBuffers(1, &triangle_bounds_buffer_);
    gl.glDeleteBuffers(1, &node_buffer_);
    node_buffer_ = 0;
    triangle_bounds_buffer_ = 0;
    garment_ranges_ = {};
    node_count_ = 0;
    garment_count_ = 0;
}
