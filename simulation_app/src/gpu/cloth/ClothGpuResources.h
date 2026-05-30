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
    GLuint triangle_normal = 0;
    GLuint vertex_normal = 0;
};

struct ClothBufferElementCounts final {
    std::uint32_t vertex = 0;
    std::uint32_t index = 0;
    std::uint32_t triangle = 0;
    std::uint32_t adjacency_entry = 0;
};

struct ClothPositionBufferView final {
    GLuint rest_position_buffer = 0;
    GLuint current_position_buffer = 0;
    GLuint previous_position_buffer = 0;
    std::uint32_t vertex_count = 0;
};

struct GarmentGpuData final {
    GarmentId id = 0;
    std::uint32_t vertex_offset = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_offset = 0;
    std::uint32_t index_count = 0;
    std::uint32_t triangle_offset = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t adjacency_entry_offset = 0;
    std::uint32_t adjacency_entry_count = 0;
};

class ClothGpuResources final {
public:
    ClothGpuResources() = default;

    ClothGpuResources(const ClothGpuResources&) = delete;
    ClothGpuResources& operator=(const ClothGpuResources&) = delete;
    ClothGpuResources(ClothGpuResources&& other) noexcept;
    ClothGpuResources& operator=(ClothGpuResources&& other) noexcept = delete;

    bool is_initialized() const;

    void sync_garments(const std::vector<GarmentObject>& garments, QOpenGLFunctions_4_5_Core& gl);
    void rebuild_compact_buffers(const std::vector<GarmentObject>& garments, QOpenGLFunctions_4_5_Core& gl);
    ClothPositionBufferView position_buffer_view() const;
    MeshTopologyResources mesh_topology_resources() const;
    MeshNormalResources mesh_normal_resources() const;
    void bind_vertex_normals(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const;
    void draw_garment(GarmentId garment_id, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    bool append_garment(const GarmentObject& garment, QOpenGLFunctions_4_5_Core& gl);
    void ensure_capacity(const ClothBufferElementCounts& required_elements, QOpenGLFunctions_4_5_Core& gl);
    bool has_gpu_objects() const;
    void create_buffers(const ClothBufferElementCounts& allocated_elements, QOpenGLFunctions_4_5_Core& gl);
    void configure_vao(QOpenGLFunctions_4_5_Core& gl);
    static void delete_buffer_set(ClothBufferSet& buffers, QOpenGLFunctions_4_5_Core& gl);
    void delete_gpu_objects(QOpenGLFunctions_4_5_Core& gl);
    const GarmentGpuData* find_garment_data(GarmentId garment_id) const;
    void take_gpu_resources_from(ClothGpuResources& other) noexcept;
    void reset_resources() noexcept;

    ClothBufferSet buffers_;
    std::vector<GarmentGpuData> garments_;
    ClothBufferElementCounts used_elements_;
    ClothBufferElementCounts allocated_elements_;
};
