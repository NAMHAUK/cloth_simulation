#pragma once

#include "gpu/bvh/BvhDataTypes.h"

#include <array>
#include <cstdint>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

struct GarmentObject;

struct ClothBvhBufferView final
{
    GLuint collision_triangle_index_buffer = 0;
    GLuint node_buffer = 0;
    GLuint triangle_bounds_buffer = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t node_count = 0;
    std::uint32_t garment_count = 0;
    const std::array<GarmentBvhRanges, 2>* garment_ranges = nullptr;
};

class ClothBvhResources final
{
public:
    ClothBvhResources() = default;
    ClothBvhResources(const ClothBvhResources&) = delete;
    ClothBvhResources& operator=(const ClothBvhResources&) = delete;

    ClothBvhBufferView buffer_view() const;
    void rebuild(const std::vector<GarmentObject>& garments, QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    GLuint collision_triangle_index_ = 0;
    GLuint bvh_node_ = 0;
    GLuint triangle_bounds_ = 0;
    std::array<GarmentBvhRanges, 2> garment_ranges_;
    std::uint32_t triangle_count_ = 0;
    std::uint32_t node_count_ = 0;
    std::uint32_t garment_count_ = 0;
};
