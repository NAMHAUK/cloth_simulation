#pragma once

#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"
#include "utils/GpuElapsedTimer.h"

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

// GPU timing stays opt-in at compile time because GL_TIME_ELAPSED has runtime cost.
#ifndef CLOTH_SIM_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
#define CLOTH_SIM_BODY_VERTEX_CLOTH_FACE_GPU_TIMING 1
#endif

class BodyVertexClothFaceCollisionSolver final {
public:
    BodyVertexClothFaceCollisionSolver() = default;
    BodyVertexClothFaceCollisionSolver(const BodyVertexClothFaceCollisionSolver&) = delete;
    BodyVertexClothFaceCollisionSolver& operator=(const BodyVertexClothFaceCollisionSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& pair_generate_shader_path,
                    const std::filesystem::path& pair_accumulate_shader_path,
                    const std::filesystem::path& pair_apply_shader_path,
                    float collision_thickness,
                    float max_correction_length,
                    QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const ClothMotionBufferView& motion_view,
                   const ClothCollisionStateBufferView& collision_view,
                   const ClothMeshTopologyResources& cloth_topology,
                   const CharacterVertexBufferView& character_vertex_view,
                   const BodyVertexBvhResources& body_vertex_bvh) const;
    void solve(const ClothMotionBufferView& motion_view,
               const ClothCollisionStateBufferView& collision_view,
               const ClothMeshTopologyResources& cloth_topology,
               const CharacterVertexBufferView& character_vertex_view,
               const BodyVertexBvhResources& body_vertex_bvh,
               QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    struct PairScratchBuffers final {
        GLuint pair_record = 0;
        GLuint pair_count = 0;
        GLuint correction_sum = 0;
        GLuint correction_count = 0;
        GLuint contact_candidate_count = 0;
        GLuint contact_candidate = 0;
        std::uint32_t vertex_capacity = 0;
        std::uint32_t pair_capacity = 0;
        std::uint32_t contact_candidate_capacity_per_vertex = 0;
    };

#if CLOTH_SIM_BODY_VERTEX_CLOTH_FACE_GPU_TIMING
    mutable GpuElapsedTimer gpu_timer_;
#endif

    bool solve_pair_path(const ClothMotionBufferView& motion_view,
                         const ClothCollisionStateBufferView& collision_view,
                         const ClothMeshTopologyResources& cloth_topology,
                         const CharacterVertexBufferView& character_vertex_view,
                         const BodyVertexBvhResources& body_vertex_bvh,
                         QOpenGLFunctions_4_5_Core& gl) const;
    bool ensure_pair_scratch_buffers(std::uint32_t vertex_count,
                                     std::uint32_t triangle_count,
                                     std::uint32_t max_contacts_per_vertex,
                                     QOpenGLFunctions_4_5_Core& gl) const;
    void clear_pair_scratch_buffers(QOpenGLFunctions_4_5_Core& gl) const;
    void release_pair_scratch_buffers(QOpenGLFunctions_4_5_Core& gl) const;
    bool has_pair_programs() const;

    GLuint pair_generate_program_ = 0;
    GLuint pair_accumulate_program_ = 0;
    GLuint pair_apply_program_ = 0;
    GLint pair_generate_triangle_count_location_ = -1;
    GLint pair_generate_root_node_index_location_ = -1;
    GLint pair_generate_max_pair_count_location_ = -1;
    GLint pair_accumulate_max_pair_count_location_ = -1;
    GLint pair_accumulate_collision_thickness_location_ = -1;
    GLint pair_accumulate_contact_candidate_capacity_location_ = -1;
    GLint pair_apply_vertex_count_location_ = -1;
    GLint pair_apply_max_contacts_per_vertex_location_ = -1;
    GLint pair_apply_contact_candidate_capacity_location_ = -1;
    GLint pair_apply_max_correction_length_location_ = -1;
    mutable PairScratchBuffers pair_scratch_buffers_;
    float collision_thickness_ = 0.0f;
    float max_correction_length_ = 0.0f;
};
