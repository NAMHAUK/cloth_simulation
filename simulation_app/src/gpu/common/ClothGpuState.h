#pragma once

#include "asset/AssetDataTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

struct GarmentObject;

struct ClothRenderBuffers final
{
    GLuint vao = 0;
    GLuint current_position = 0;
    GLuint triangle_vertex_indices = 0;
    GLuint vertex_normal = 0;
};

struct GarmentBufferState final
{
    std::uint32_t vertex_start_index = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_start_index = 0;
    std::uint32_t triangle_count = 0;
};

struct ClothBufferElementCounts final
{
    std::uint32_t vertex = 0;
    std::uint32_t triangle = 0;
};

class ClothGpuState final
{
public:
    ClothGpuState() = default;

    ClothGpuState(const ClothGpuState&) = delete;
    ClothGpuState& operator=(const ClothGpuState&) = delete;
    ClothGpuState& operator=(ClothGpuState&& other) noexcept = delete;

    std::vector<TriangleVertexIndices> rebuild_buffers(const std::vector<GarmentObject>& garments,
                                                       GarmentLayer changed_layer,
                                                       QOpenGLFunctions_4_5_Core& gl);
    std::vector<TriangleVertexIndices> upload_triangle_indices(const std::vector<GarmentObject>& garments,
                                                               QOpenGLFunctions_4_5_Core& gl);
    void upload_garment_placement(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl);
    void capture_base_positions(QOpenGLFunctions_4_5_Core& gl);
    void restore_base_positions(QOpenGLFunctions_4_5_Core& gl) const;
    void clear_base_positions(QOpenGLFunctions_4_5_Core& gl);

    void draw_garment(GarmentLayer layer, QOpenGLFunctions_4_5_Core& gl) const;

    bool is_initialized() const;

    const std::array<GarmentBufferState, garment_layer_count>& garment_states() const;
    const ClothBufferElementCounts& element_counts() const;
    const ClothRenderBuffers& buffer_set() const;

    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    struct BufferState final
    {
        ClothRenderBuffers buffers;
        std::array<GarmentBufferState, garment_layer_count> garments;
        ClothBufferElementCounts element_counts;
    };

    static void assign_garment_buffer_states(const std::vector<GarmentObject>& garments, BufferState& state);
    void create_dynamic_buffers(const std::vector<GarmentObject>& garments,
                                GarmentLayer changed_layer,
                                BufferState& rebuild_state,
                                QOpenGLFunctions_4_5_Core& gl) const;
    void copy_current_positions(GarmentLayer layer,
                                const BufferState& rebuild_state,
                                QOpenGLFunctions_4_5_Core& gl) const;
    void configure_vao(QOpenGLFunctions_4_5_Core& gl);

    bool has_gpu_objects() const;

    static void delete_buffer_set(ClothRenderBuffers& buffers, QOpenGLFunctions_4_5_Core& gl);
    void reset_resources() noexcept;

    BufferState state_;
    GLuint base_positions_ = 0;
    std::uint32_t base_position_vertex_count_ = 0;
};
