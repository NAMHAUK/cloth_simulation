#pragma once

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class CharacterGpuResources;
class BodyBvhBoundsUpdater;
struct CharacterMeshTopologyResources;
struct CharacterVertexBufferView;
struct BodyTriangleResources;

class CharacterGpuStateUpdater final
{
public:
    CharacterGpuStateUpdater(CharacterGpuResources& character_gpu_state,
                             BodyBvhBoundsUpdater& bvh_bounds_updater);
    CharacterGpuStateUpdater(const CharacterGpuStateUpdater&) = delete;
    CharacterGpuStateUpdater& operator=(const CharacterGpuStateUpdater&) = delete;

    bool is_initialized() const;
    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

    void initialize_character_pose_state(QOpenGLFunctions_4_5_Core& gl) const;
    void update_character_pose_state(std::uint32_t frame_index,
                                     float frame_alpha,
                                     QOpenGLFunctions_4_5_Core& gl) const;

private:
    void write_current_position_buffer(float frame_alpha,
                                       const CharacterVertexBufferView& vertex_view,
                                       QOpenGLFunctions_4_5_Core& gl) const;
    void copy_current_position_to_previous(const CharacterVertexBufferView& vertex_view,
                                           QOpenGLFunctions_4_5_Core& gl) const;
    void update_triangle_geometry(const CharacterMeshTopologyResources& topology,
                                  const CharacterVertexBufferView& vertex_view,
                                  const BodyTriangleResources& body_triangles,
                                  QOpenGLFunctions_4_5_Core& gl) const;
    void update_derived_pose_state(QOpenGLFunctions_4_5_Core& gl) const;

    CharacterGpuResources& character_gpu_state_;
    BodyBvhBoundsUpdater& bvh_bounds_updater_;

    GLuint position_program_ = 0;
    GLuint triangle_geometry_program_ = 0;

    GLint position_current_frame_base_location_ = -1;
    GLint position_next_frame_base_location_ = -1;
    GLint position_frame_alpha_location_ = -1;
    GLint position_vertex_count_location_ = -1;
    GLint triangle_count_location_ = -1;
};
