#pragma once

#include "io/GarmentAsset.h"

#include <QOpenGLFunctions_4_5_Core>

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
    GLsizei index_count_ = 0;
};
