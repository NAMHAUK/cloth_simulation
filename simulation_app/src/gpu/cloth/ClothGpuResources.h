#pragma once

#include "gpu/bvh/BvhDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"

#include <array>
#include <cstdint>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

struct GarmentObject;

class ClothGpuResources final
{
public:
    ClothGpuResources() = default;

    ClothGpuResources(const ClothGpuResources&) = delete;
    ClothGpuResources& operator=(const ClothGpuResources&) = delete;
    ClothGpuResources& operator=(ClothGpuResources&& other) noexcept = delete;

    void rebuild_buffers(const std::vector<GarmentObject>& garments,
                         GarmentLayer changed_layer,
                         QOpenGLFunctions_4_5_Core& gl);
    void upload_garment_placement(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl);
    void upload_attachment_indices(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl);
    void activate_attachment_targets(GarmentLayer layer);
    void capture_base_positions(QOpenGLFunctions_4_5_Core& gl);
    void restore_base_positions(QOpenGLFunctions_4_5_Core& gl) const;
    void clear_base_positions(QOpenGLFunctions_4_5_Core& gl);
    void copy_current_positions_to_previous(QOpenGLFunctions_4_5_Core& gl) const;

    void bind_vertex_normals(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const;
    void draw_garment(GarmentLayer layer, QOpenGLFunctions_4_5_Core& gl) const;

    bool is_initialized() const;

    const std::array<GarmentBufferState, 2>& garment_buffer_states() const;
    ClothMotionBufferView motion_buffer_view() const;
    ClothCollisionPushoutBufferView collision_pushout_buffer_view() const;
    ClothContactMotionBufferView contact_motion_buffer_view() const;
    ClothBodyTriangleIndexBufferView body_triangle_index_buffer_view() const;
    DistanceConstraintBufferView stretch_constraint_buffer_view() const;
    DistanceConstraintBufferView bending_constraint_buffer_view() const;
    AttachmentConstraintBufferView attachment_constraint_buffer_view() const;
    ClothMeshTopologyResources mesh_topology_resources() const;
    ClothNormalResources mesh_normal_resources() const;
    BvhBufferView cloth_bvh_buffer_view() const;

    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    struct BufferState final
    {
        ClothBufferSet buffers;
        std::array<GarmentBufferState, 2> garments;
        std::vector<ConstraintColorState> stretch_color_states;
        std::vector<ConstraintColorState> bending_color_states;
        ClothBufferElementCounts element_counts;
    };

    static void assign_garment_buffer_states(const std::vector<GarmentObject>& garments, BufferState& state);
    void create_dynamic_buffers(const std::vector<GarmentObject>& garments,
                                GarmentLayer changed_layer,
                                BufferState& rebuild_state,
                                QOpenGLFunctions_4_5_Core& gl) const;
    void copy_dynamic_state_buffers(GarmentLayer layer,
                                    const BufferState& rebuild_state,
                                    QOpenGLFunctions_4_5_Core& gl) const;
    void copy_attachment_target_state(GarmentLayer layer,
                                      BufferState& rebuild_state,
                                      QOpenGLFunctions_4_5_Core& gl) const;
    static void create_topology_buffers(const std::vector<GarmentObject>& garments,
                                        BufferState& rebuild_state,
                                        QOpenGLFunctions_4_5_Core& gl);
    static void create_bvh_buffers(const std::vector<GarmentObject>& garments,
                                   BufferState& rebuild_state,
                                   QOpenGLFunctions_4_5_Core& gl);
    static void create_distance_constraint_buffers(const std::vector<GarmentObject>& garments,
                                                   BufferState& rebuild_state,
                                                   QOpenGLFunctions_4_5_Core& gl);
    void configure_vao(QOpenGLFunctions_4_5_Core& gl);

    bool has_gpu_objects() const;

    static void delete_buffer_set(ClothBufferSet& buffers, QOpenGLFunctions_4_5_Core& gl);
    void reset_resources() noexcept;

    BufferState state_;
    GLuint base_positions_ = 0;
    std::uint32_t base_position_vertex_count_ = 0;
};
