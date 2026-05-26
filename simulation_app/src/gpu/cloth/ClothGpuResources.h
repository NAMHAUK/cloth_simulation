#pragma once

#include "asset/GarmentAsset.h"

#include <QOpenGLFunctions_4_5_Core>

struct NormalUpdateInputs;

class ClothGpuResources final {
public:
    ClothGpuResources() = default;

    ClothGpuResources(const ClothGpuResources&) = delete;
    ClothGpuResources& operator=(const ClothGpuResources&) = delete;
    ClothGpuResources(ClothGpuResources&& other) noexcept;
    ClothGpuResources& operator=(ClothGpuResources&& other) noexcept;

    bool is_initialized() const;

    void upload(const GarmentMesh& garment_mesh, QOpenGLFunctions_4_5_Core& gl);
    void reset_states(const GarmentMesh& garment_mesh, QOpenGLFunctions_4_5_Core& gl);
    NormalUpdateInputs normal_update_inputs() const;
    void bind_vertex_normals(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const;
    void draw(QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    void initialize_gpu_resources(QOpenGLFunctions_4_5_Core& gl);
    void take_gpu_resources_from(ClothGpuResources& other) noexcept;
    void reset_resources() noexcept;

    GLuint vao_ = 0;
    GLuint rest_position_buffer_ = 0;
    GLuint current_position_buffer_ = 0;
    GLuint previous_position_buffer_ = 0;
    GLuint index_buffer_ = 0;
    GLuint adjacency_offset_buffer_ = 0;
    GLuint adjacency_triangle_buffer_ = 0;
    GLuint triangle_normal_buffer_ = 0;
    GLuint vertex_normal_buffer_ = 0;
    std::uint32_t vertex_count_ = 0;
    std::uint32_t triangle_count_ = 0;
    GLsizei index_count_ = 0;
};
