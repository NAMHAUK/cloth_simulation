#pragma once

#include "gpu/cloth/ClothGpuDataTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

class ClothGpuState;
struct GarmentObject;

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

    const PbdClothBufferSet& buffer_set() const;
    const std::array<AttachmentBufferState, garment_layer_count>& attachment_states() const;
    const std::array<GarmentCollisionBufferState, garment_layer_count>& collision_states() const;
    std::uint32_t edge_count() const;
    std::size_t max_bvh_level_count() const;
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

    bool has_gpu_objects() const;

    void release_dynamic_buffers(QOpenGLFunctions_4_5_Core& gl);
    void release_attachment_buffers(QOpenGLFunctions_4_5_Core& gl);
    void release_normal_buffers(QOpenGLFunctions_4_5_Core& gl);

    PbdClothBufferSet buffers_;
    std::array<GarmentCollisionBufferState, garment_layer_count> collision_states_;
    std::size_t max_bvh_level_count_ = 0;
    std::array<AttachmentBufferState, garment_layer_count> attachment_states_;
    std::vector<ConstraintColorState> stretch_color_states_;
    std::vector<ConstraintColorState> bending_color_states_;
};
