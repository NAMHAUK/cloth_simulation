#pragma once

#include "assets/GarmentAsset.h"

#include <QOpenGLFunctions_4_5_Core>

class ClothGpuState final {
public:
    ClothGpuState() = default;

    ClothGpuState(const ClothGpuState&) = delete;
    ClothGpuState& operator=(const ClothGpuState&) = delete;
    ClothGpuState(ClothGpuState&& other) noexcept;
    ClothGpuState& operator=(ClothGpuState&& other) noexcept;

    bool is_initialized() const;

    void upload(const GarmentMesh& garment_mesh, QOpenGLFunctions_4_5_Core& gl);
    void reset_states(const GarmentMesh& garment_mesh, QOpenGLFunctions_4_5_Core& gl);
    void draw(QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    void initialize_gpu_resources(QOpenGLFunctions_4_5_Core& gl);
    void take_gpu_resources_from(ClothGpuState& other) noexcept;
    void reset_resources() noexcept;

    GLuint vao_ = 0;
    GLuint rest_position_buffer_ = 0;
    GLuint current_position_buffer_ = 0;
    GLuint previous_position_buffer_ = 0;
    GLuint index_buffer_ = 0;
    GLsizei index_count_ = 0;
};
