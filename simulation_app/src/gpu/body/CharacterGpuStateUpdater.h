#pragma once

#include "gpu/body/bvh/BvhDataTypes.h"

#include <cstdint>
#include <filesystem>
#include <vector>

#include <QOpenGLFunctions_4_5_Core>

class CharacterGpuResources;
class CharacterBvhBoundsUpdater;
class NormalUpdater;
struct CharacterFrameInterpolation;
struct CharacterMeshTopologyResources;
struct CharacterVertexBufferView;
struct TriangleGeometryResources;

class CharacterGpuStateUpdater final {
public:
    CharacterGpuStateUpdater(CharacterGpuResources& character_gpu_state,
                             CharacterBvhBoundsUpdater& bvh_bounds_updater,
                             NormalUpdater& normal_updater);
    CharacterGpuStateUpdater(const CharacterGpuStateUpdater&) = delete;
    CharacterGpuStateUpdater& operator=(const CharacterGpuStateUpdater&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& position_shader_path,
                    const std::filesystem::path& triangle_geometry_shader_path,
                    QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

    void initialize_character_pose_state(const CharacterFrameInterpolation& interpolation,
                                         const std::vector<BvhNodeRange>& node_ranges_by_level,
                                         const std::vector<BvhNodeRange>& body_vertex_node_ranges_by_level,
                                         const std::vector<BvhNodeRange>& body_edge_node_ranges_by_level,
                                         float collision_thickness,
                                         QOpenGLFunctions_4_5_Core& gl) const;
    void update_character_pose_state(const CharacterFrameInterpolation& interpolation,
                                     const std::vector<BvhNodeRange>& node_ranges_by_level,
                                     const std::vector<BvhNodeRange>& body_vertex_node_ranges_by_level,
                                     const std::vector<BvhNodeRange>& body_edge_node_ranges_by_level,
                                     float collision_thickness,
                                     QOpenGLFunctions_4_5_Core& gl) const;

private:
    void write_current_position_buffer(const CharacterFrameInterpolation& interpolation,
                                       const CharacterVertexBufferView& vertex_view,
                                       QOpenGLFunctions_4_5_Core& gl) const;
    void copy_current_position_to_previous(const CharacterVertexBufferView& vertex_view,
                                           QOpenGLFunctions_4_5_Core& gl) const;
    void update_triangle_geometry(const CharacterMeshTopologyResources& topology,
                                  const CharacterVertexBufferView& vertex_view,
                                  const TriangleGeometryResources& triangle_geometry,
                                  QOpenGLFunctions_4_5_Core& gl) const;
    void update_derived_pose_state(const std::vector<BvhNodeRange>& node_ranges_by_level,
                                   const std::vector<BvhNodeRange>& body_vertex_node_ranges_by_level,
                                   const std::vector<BvhNodeRange>& body_edge_node_ranges_by_level,
                                   float collision_thickness,
                                   QOpenGLFunctions_4_5_Core& gl) const;

    CharacterGpuResources& character_gpu_state_;
    CharacterBvhBoundsUpdater& bvh_bounds_updater_;
    NormalUpdater& normal_updater_;

    GLuint position_program_ = 0;
    GLuint triangle_geometry_program_ = 0;

    GLint position_current_frame_base_location_ = -1;
    GLint position_next_frame_base_location_ = -1;
    GLint position_frame_alpha_location_ = -1;
    GLint position_vertex_count_location_ = -1;
    GLint triangle_count_location_ = -1;
};
