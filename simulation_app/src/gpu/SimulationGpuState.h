#pragma once

#include "gpu/CharacterGpuState.h"
#include "gpu/ClothGpuState.h"
#include "gpu/GridGpuState.h"
#include "rendering/ViewerShaderProgram.h"

#include <cstdint>
#include <filesystem>
#include <vector>

#include <glm/mat4x4.hpp>

#include <QOpenGLFunctions_4_5_Core>

class SimulationScene;

class SimulationGpuState final {
public:
    SimulationGpuState() = default;
    SimulationGpuState(const SimulationGpuState&) = delete;
    SimulationGpuState& operator=(const SimulationGpuState&) = delete;

    bool is_initialized() const;

    bool initialize(const std::filesystem::path& vertex_shader_path,
                    const std::filesystem::path& fragment_shader_path,
                    QOpenGLFunctions_4_5_Core& gl);
    void sync(const SimulationScene& scene, QOpenGLFunctions_4_5_Core& gl);
    void draw(const SimulationScene& scene, const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    void sync_character(const SimulationScene& scene, QOpenGLFunctions_4_5_Core& gl);
    void sync_garments(const SimulationScene& scene, QOpenGLFunctions_4_5_Core& gl);

    ViewerShaderProgram viewer_shader_;
    GridGpuState grid_gpu_state_;
    CharacterGpuState character_gpu_state_;
    std::vector<ClothGpuState> garment_gpu_states_;
    std::vector<std::uint64_t> uploaded_garment_revisions_;

    std::uint64_t uploaded_character_revision_ = 0;
    std::uint32_t uploaded_character_frame_ = 0;
    bool character_uploaded_ = false;
};
