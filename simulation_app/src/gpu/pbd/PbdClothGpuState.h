#pragma once

#include "asset/AssetDataTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

class ClothGpuState;
struct GarmentObject;

struct PbdClothBufferSet final
{
    GLuint adjacent_triangle_offsets = 0;
    GLuint adjacent_triangle_indices = 0;
    GLuint triangle_normal = 0;
    GLuint triangle_bvh_node = 0;
    GLuint triangle_bounds = 0;
    GLuint vertex_face_exclusions = 0;
    GLuint edge_vertex_indices = 0;
    GLuint edge_bounds = 0;
    GLuint edge_bvh_node = 0;
    GLuint edge_exclusions = 0;
    GLuint previous_position = 0;
    GLuint collision_pushout = 0;
    GLuint inward_motion_correction = 0;
    GLuint stretch_edge_vertex_indices = 0;
    GLuint stretch_rest_length = 0;
    GLuint bending_edge_vertex_indices = 0;
    GLuint bending_rest_length = 0;
    GLuint attachment_indices = 0;
    GLuint attachment_barycentric_offset = 0;
};

struct AttachmentBufferState final
{
    std::uint32_t start_index = 0;
    std::uint32_t count = 0;
    std::uint32_t active_count = 0;
};

struct GarmentCollisionBufferState final
{
    std::uint32_t vertex_face_exclusion_offset = 0;
    std::uint32_t edge_start_index = 0;
    std::uint32_t edge_count = 0;
    std::uint32_t edge_exclusion_offset = 0;
    std::vector<std::uint32_t> triangle_bvh_level_offsets;
    std::vector<std::uint32_t> edge_bvh_level_offsets;
};

class PbdClothGpuState final
{
public:
    PbdClothGpuState() = default;
    PbdClothGpuState(const PbdClothGpuState&) = delete;
    PbdClothGpuState& operator=(const PbdClothGpuState&) = delete;

    void rebuild_dynamic_buffers(const ClothGpuState& cloth_state, QOpenGLFunctions_4_5_Core& gl);
    void reset_velocity(const ClothGpuState& cloth_state,
                        std::uint32_t vertex_start_index,
                        std::uint32_t vertex_count,
                        QOpenGLFunctions_4_5_Core& gl) const;

    void rebuild_attachment_buffers(const std::vector<GarmentObject>& garments,
                                    const std::vector<const GarmentObject*>& placement_garments,
                                    QOpenGLFunctions_4_5_Core& gl);
    void upload_attachment_indices(const GarmentObject& garment,
                                   const ClothGpuState& cloth_state,
                                   QOpenGLFunctions_4_5_Core& gl);
    void activate_attachment_targets(GarmentLayer layer);

    void rebuild_normal_buffers(const std::vector<TriangleVertexIndices>& triangle_vertex_indices,
                                std::uint32_t vertex_count,
                                QOpenGLFunctions_4_5_Core& gl);

    void create_collision_buffers(const std::vector<GarmentObject>& garments,
                                  const ClothGpuState& cloth_state,
                                  QOpenGLFunctions_4_5_Core& gl);

    void create_distance_constraint_buffers(const std::vector<GarmentObject>& garments,
                                            const ClothGpuState& cloth_state,
                                            QOpenGLFunctions_4_5_Core& gl);

    bool is_initialized() const;
    bool has_gpu_objects() const;

    const PbdClothBufferSet& buffer_set() const;
    const std::array<AttachmentBufferState, garment_layer_count>& attachment_states() const;
    const std::array<GarmentCollisionBufferState, garment_layer_count>& collision_states() const;
    std::uint32_t edge_count() const;
    std::size_t max_bvh_level_count() const;
    std::uint32_t stretch_constraint_count() const;
    std::uint32_t bending_constraint_count() const;
    const std::vector<ConstraintColorState>& stretch_color_states() const;
    const std::vector<ConstraintColorState>& bending_color_states() const;

    void release(QOpenGLFunctions_4_5_Core& gl);
    void release_collision_buffers(QOpenGLFunctions_4_5_Core& gl);
    void release_distance_constraint_buffers(QOpenGLFunctions_4_5_Core& gl);

private:
    void create_bvh_buffers(const std::vector<GarmentObject>& garments,
                            const ClothGpuState& cloth_state,
                            QOpenGLFunctions_4_5_Core& gl);
    void create_exclusion_buffers(const std::vector<GarmentObject>& garments,
                                  const ClothGpuState& cloth_state,
                                  QOpenGLFunctions_4_5_Core& gl);

    void release_dynamic_buffers(QOpenGLFunctions_4_5_Core& gl);
    void release_attachment_buffers(QOpenGLFunctions_4_5_Core& gl);
    void release_normal_buffers(QOpenGLFunctions_4_5_Core& gl);

    PbdClothBufferSet buffers_;
    std::array<GarmentCollisionBufferState, garment_layer_count> collision_states_;
    std::size_t max_bvh_level_count_ = 0;
    std::array<AttachmentBufferState, garment_layer_count> attachment_states_;
    std::uint32_t stretch_constraint_count_ = 0;
    std::uint32_t bending_constraint_count_ = 0;
    std::vector<ConstraintColorState> stretch_color_states_;
    std::vector<ConstraintColorState> bending_color_states_;
};
