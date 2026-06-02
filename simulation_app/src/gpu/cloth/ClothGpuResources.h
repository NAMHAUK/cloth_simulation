#pragma once

#include "scene/SceneState.h"

#include <cstdint>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

struct MeshNormalResources;
struct MeshTopologyResources;

struct ClothBufferSet final {
    GLuint vao = 0;
    GLuint rest_position = 0;
    GLuint current_position = 0;
    GLuint previous_position = 0;
    GLuint index = 0;
    GLuint adjacency_offset = 0;
    GLuint adjacency_triangle = 0;
    GLuint stretch_edge_index = 0;
    GLuint stretch_rest_length = 0;
    GLuint triangle_normal = 0;
    GLuint vertex_normal = 0;
};

struct ConstraintRange final {
    std::uint32_t offset = 0;
    std::uint32_t count = 0;
};

struct ClothBufferElementCounts final {
    std::uint32_t vertex = 0;
    std::uint32_t index = 0;
    std::uint32_t triangle = 0;
    std::uint32_t adjacency_entry = 0;
    std::uint32_t stretch_constraint = 0;
};

struct ClothPositionBufferView final {
    GLuint rest_position_buffer = 0;
    GLuint current_position_buffer = 0;
    GLuint previous_position_buffer = 0;
    std::uint32_t vertex_count = 0;
};

struct StretchConstraintBufferView final {
    GLuint edge_index_buffer = 0;
    GLuint rest_length_buffer = 0;
    std::uint32_t constraint_count = 0;
    const std::vector<ConstraintRange>* color_ranges = nullptr;
};

struct GarmentBufferRanges final {
    GarmentId id = 0;
    std::uint32_t vertex_offset = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_offset = 0;
    std::uint32_t index_count = 0;
    std::uint32_t triangle_offset = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t adjacency_entry_offset = 0;
    std::uint32_t adjacency_entry_count = 0;
    std::uint32_t stretch_constraint_offset = 0;
    std::uint32_t stretch_constraint_count = 0;
};

class ClothGpuResources final {
public:
    ClothGpuResources() = default;

    ClothGpuResources(const ClothGpuResources&) = delete;
    ClothGpuResources& operator=(const ClothGpuResources&) = delete;
    ClothGpuResources(ClothGpuResources&& other) noexcept;
    ClothGpuResources& operator=(ClothGpuResources&& other) noexcept = delete;

    bool is_initialized() const;

    void update_garment_buffers(const std::vector<GarmentObject>& garments, QOpenGLFunctions_4_5_Core& gl);
    ClothPositionBufferView position_buffer_view() const;
    StretchConstraintBufferView stretch_constraint_buffer_view() const;
    MeshTopologyResources mesh_topology_resources() const;
    MeshNormalResources mesh_normal_resources() const;
    void bind_vertex_normals(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const;
    void draw_garment(GarmentId garment_id, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    void rebuild_garment_buffers(const std::vector<GarmentObject>& garments, QOpenGLFunctions_4_5_Core& gl);
    void replace_with_rebuild_buffers(ClothBufferSet rebuild_buffer_set,
                                      std::vector<GarmentBufferRanges> rebuild_ranges,
                                      std::vector<ConstraintRange> rebuild_color_ranges,
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
    const GarmentBufferRanges* find_garment_buffer_ranges(GarmentId garment_id) const;
    void reset_resources() noexcept;

    ClothBufferSet buffers_;
    std::vector<GarmentBufferRanges> garments_;
    std::vector<ConstraintRange> stretch_color_ranges_;
    ClothBufferElementCounts used_elements_;
    ClothBufferElementCounts allocated_elements_;
};
