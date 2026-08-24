#pragma once

#include "asset/AssetDataTypes.h"
#include "gpu/bvh/BodyBvhBoundsUpdater.h"
#include "gpu/bvh/BvhDataTypes.h"
#include "gpu/character/CharacterGpuDataTypes.h"

#include <QOpenGLFunctions_4_5_Core>
#include <cstdint>
#include <filesystem>

class CharacterGpuState final
{
public:
    CharacterGpuState() = default;
    CharacterGpuState(const CharacterGpuState&) = delete;
    CharacterGpuState& operator=(const CharacterGpuState&) = delete;

    void initialize(const std::filesystem::path& shader_dir,
                    float body_detection_distance,
                    QOpenGLFunctions_4_5_Core& gl);

    void initialize_mesh(const CharacterMotion& character_motion,
                         const Bvh& body_triangle_bvh,
                         const Bvh& body_vertex_bvh,
                         const Bvh& body_edge_bvh,
                         QOpenGLFunctions_4_5_Core& gl);

    void set_motion(const CharacterMotion& character_motion, QOpenGLFunctions_4_5_Core& gl);
    void update_pose(std::uint32_t frame_index, float frame_alpha, QOpenGLFunctions_4_5_Core& gl);

    void draw(QOpenGLFunctions_4_5_Core& gl) const;
    void bind_current_positions(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const;
    void bind_vertex_normals(GLuint binding_index, QOpenGLFunctions_4_5_Core& gl) const;

    CharacterMeshTopologyResources mesh_topology_resources() const;
    CharacterVertexBufferView vertex_buffer_view() const;
    BodyTriangleResources body_triangle_resources() const;
    CharacterNormalResources mesh_normal_resources() const;
    BvhBufferView body_triangle_bvh_buffer_view() const;
    BvhBufferView body_vertex_bvh_buffer_view() const;
    BvhBufferView body_edge_bvh_buffer_view() const;

    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    void initialize_gpu_resources(QOpenGLFunctions_4_5_Core& gl);

    void write_current_positions(float frame_alpha, QOpenGLFunctions_4_5_Core& gl) const;
    void copy_current_to_previous(QOpenGLFunctions_4_5_Core& gl) const;
    void update_derived_pose(QOpenGLFunctions_4_5_Core& gl) const;
    void update_triangle_geometry(QOpenGLFunctions_4_5_Core& gl) const;

    void release_mesh_resources(QOpenGLFunctions_4_5_Core& gl);
    void reset_resources() noexcept;

    BodyBvhBoundsUpdater bvh_bounds_updater_;
    GLuint position_program_ = 0;
    GLuint triangle_update_program_ = 0;
    GLint position_current_frame_base_location_ = -1;
    GLint position_next_frame_base_location_ = -1;
    GLint position_frame_alpha_location_ = -1;
    GLint position_vertex_count_location_ = -1;
    GLint triangle_count_location_ = -1;

    GLuint vao_ = 0;
    GLsizei index_count_ = 0;

    CharacterBufferSet buffers_;

    std::uint32_t frame_count_ = 0;
    std::uint32_t vertex_count_ = 0;
    std::uint32_t current_frame_index_ = 0;

    std::uint32_t triangle_count_ = 0;
    glm::uvec4 arm_triangle_ranges_{};
};
