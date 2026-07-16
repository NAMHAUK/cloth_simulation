#pragma once

#include "gpu/cloth/ClothGpuDataTypes.h"

#include <cstdint>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

struct GarmentObject;

class ClothGpuResources final {
public:
    ClothGpuResources() = default;

    ClothGpuResources(const ClothGpuResources&) = delete;
    ClothGpuResources& operator=(const ClothGpuResources&) = delete;
    ClothGpuResources(ClothGpuResources&& other) noexcept;
    ClothGpuResources& operator=(ClothGpuResources&& other) noexcept = delete;

    bool is_initialized() const;

    void update_garment_buffers(const std::vector<GarmentObject>& garments, QOpenGLFunctions_4_5_Core& gl);
    bool update_garment_placement(const GarmentObject& garment,
                                  bool update_rest_lengths,
                                  QOpenGLFunctions_4_5_Core& gl);
    bool upload_garment_attachment_vertices(const GarmentObject& garment,
                                            ElementRange& target_range,
                                            QOpenGLFunctions_4_5_Core& gl);
    bool activate_attachment_targets(const ElementRange& target_range);
    bool save_base_positions(QOpenGLFunctions_4_5_Core& gl);
    bool restore_base_positions(QOpenGLFunctions_4_5_Core& gl) const;
    void clear_base_positions(QOpenGLFunctions_4_5_Core& gl);
    const std::vector<GarmentBufferRanges>& garment_buffer_ranges() const;
    ClothMotionBufferView motion_buffer_view() const;
    ClothCollisionPushoutBufferView collision_pushout_buffer_view() const;
    ClothBodyTriangleIdBufferView body_triangle_id_buffer_view() const;
    void copy_current_positions_to_previous(QOpenGLFunctions_4_5_Core& gl) const;
    DistanceConstraintBufferView stretch_constraint_buffer_view() const;
    DistanceConstraintBufferView bending_constraint_buffer_view() const;
    AttachmentConstraintBufferView attachment_constraint_buffer_view() const;
    ClothMeshTopologyResources mesh_topology_resources() const;
    ClothNormalResources mesh_normal_resources() const;
    void bind_vertex_normals(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const;
    void draw_garment(std::uint32_t garment_id, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    void rebuild_buffers(const std::vector<GarmentObject>& garments, QOpenGLFunctions_4_5_Core& gl);
    void replace_with_rebuild_buffers(ClothBufferSet rebuild_buffer_set,
                                      std::vector<GarmentBufferRanges> rebuild_ranges,
                                      std::vector<ElementRange> rebuild_stretch_color_ranges,
                                      std::vector<ElementRange> rebuild_bending_color_ranges,
                                      std::vector<ElementRange> rebuild_attachment_ranges,
                                      const ClothBufferElementCounts& rebuild_element_counts,
                                      QOpenGLFunctions_4_5_Core& gl);
    bool append_garment(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl);
    void ensure_capacity(const ClothBufferElementCounts& required_elements, QOpenGLFunctions_4_5_Core& gl);
    bool has_enough_capacity(const ClothBufferElementCounts& required_elements) const;
    bool has_gpu_objects() const;
    void create_buffers(const ClothBufferElementCounts& allocated_elements, QOpenGLFunctions_4_5_Core& gl);
    void configure_vao(QOpenGLFunctions_4_5_Core& gl);
    static void delete_buffer_set(ClothBufferSet& buffers, QOpenGLFunctions_4_5_Core& gl);
    void delete_gpu_objects(QOpenGLFunctions_4_5_Core& gl);
    const GarmentBufferRanges* find_garment_buffer_ranges(std::uint32_t garment_id) const;
    void reset_resources() noexcept;

    ClothBufferSet buffers_;
    GLuint base_positions_ = 0;
    std::vector<GarmentBufferRanges> garments_;
    std::vector<ElementRange> stretch_color_ranges_;
    std::vector<ElementRange> bending_color_ranges_;
    std::vector<ElementRange> attachment_ranges_;
    ClothBufferElementCounts used_elements_;
    ClothBufferElementCounts allocated_elements_;
    std::uint32_t base_position_vertex_count_ = 0;
};
