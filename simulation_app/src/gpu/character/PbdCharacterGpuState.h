#pragma once

#include "asset/AssetDataTypes.h"
#include "gpu/character/CharacterGpuDataTypes.h"

#include <cstdint>
#include <filesystem>

class CharacterGpuState;
class SceneState;

class PbdCharacterGpuState final
{
public:
    PbdCharacterGpuState() = default;
    PbdCharacterGpuState(const PbdCharacterGpuState&) = delete;
    PbdCharacterGpuState& operator=(const PbdCharacterGpuState&) = delete;

    void initialize(const std::filesystem::path& shader_dir,
                    const SceneState& scene,
                    const CharacterGpuState& character_state,
                    QOpenGLFunctions_4_5_Core& gl);
    void set_motion(const CharacterMotion& motion,
                    const CharacterGpuState& character_state,
                    QOpenGLFunctions_4_5_Core& gl);
    void update_pose(std::uint32_t frame_index,
                     float frame_alpha,
                     const CharacterGpuState& character_state,
                     QOpenGLFunctions_4_5_Core& gl);

    const PbdCharacterBufferSet& buffer_set() const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    void write_current_positions(float frame_alpha,
                                 const CharacterGpuState& character_state,
                                 QOpenGLFunctions_4_5_Core& gl) const;
    void copy_current_to_previous(const CharacterGpuState& character_state, QOpenGLFunctions_4_5_Core& gl) const;
    void update_triangle_geometry(const CharacterGpuState& character_state, QOpenGLFunctions_4_5_Core& gl) const;

    GLuint position_program_ = 0;
    GLuint triangle_update_program_ = 0;
    GLint position_current_frame_base_loc_ = -1;
    GLint position_next_frame_base_loc_ = -1;
    GLint position_frame_alpha_loc_ = -1;
    GLint position_vertex_count_loc_ = -1;
    GLint triangle_count_loc_ = -1;

    PbdCharacterBufferSet buffers_;
    std::uint32_t frame_count_ = 0;
    std::uint32_t current_frame_index_ = 0;
};
