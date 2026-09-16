#include "simulation/collision/ClothClothCollisionSolver.h"

#include "gpu/scene/SceneGpuState.h"
#include "simulation/SimulationParams.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <stdexcept>

namespace {
constexpr std::uint32_t apply_local_size = 128u;
}

ClothClothCollisionSolver::ClothClothCollisionSolver(const ClothCollisionParams& params)
    : collision_thickness_(params.thickness),
      collision_stiffness_(params.stiffness),
      max_correction_length_(params.max_correction_length),
      surface_search_radius_(params.body_search_radius)
{}

void ClothClothCollisionSolver::initialize(const std::filesystem::path& shader_dir,
                                           QOpenGLFunctions_4_5_Core& gl)
{
    const auto collision_dir = shader_dir / "collision" / "cloth_cloth";

    {
        auto& shader = accumulate_;
        shader.program = load_compute_program(collision_dir / "vertex_face_accumulate.comp", gl);

        shader.max_candidates_loc = require_uniform_location(shader.program, "uMaxCandidateCount", gl);
        shader.upper_vertex_offset_loc = require_uniform_location(shader.program, "uUpperVertexOffset", gl);
        const GLint thickness_loc = require_uniform_location(shader.program, "uCollisionThickness", gl);
        const GLint stiffness_loc = require_uniform_location(shader.program, "uCollisionStiffness", gl);
        gl.glProgramUniform1f(shader.program, thickness_loc, collision_thickness_);
        gl.glProgramUniform1f(shader.program, stiffness_loc, collision_stiffness_);
    }

    {
        auto& shader = initial_accumulate_;
        shader.program = load_compute_program(collision_dir / "initial_layer_accumulate.comp", gl);

        shader.max_candidates_loc = require_uniform_location(shader.program, "uMaxCandidateCount", gl);
        shader.upper_vertex_offset_loc = require_uniform_location(shader.program, "uUpperVertexOffset", gl);
        const GLint thickness_loc = require_uniform_location(shader.program, "uCollisionThickness", gl);
        const GLint stiffness_loc = require_uniform_location(shader.program, "uCollisionStiffness", gl);
        const GLint search_radius_loc = require_uniform_location(shader.program, "uSearchRadiusSquared", gl);
        gl.glProgramUniform1f(shader.program, thickness_loc, collision_thickness_);
        gl.glProgramUniform1f(shader.program, stiffness_loc, collision_stiffness_);
        gl.glProgramUniform1f(shader.program,
                              search_radius_loc,
                              surface_search_radius_ * surface_search_radius_);
    }

    {
        apply_program_ = load_compute_program(collision_dir / "apply.comp", gl);

        cloth_vertex_count_loc_ = require_uniform_location(apply_program_, "uVertexCount", gl);
        const GLint max_correction_loc = require_uniform_location(apply_program_, "uMaxCorrectionLength", gl);
        gl.glProgramUniform1f(apply_program_, max_correction_loc, max_correction_length_);
    }
}

void ClothClothCollisionSolver::solve(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const
{
    solve(gpu_state, accumulate_, gl);
}

void ClothClothCollisionSolver::solve_initial(const SceneGpuState& gpu_state,
                                              QOpenGLFunctions_4_5_Core& gl) const
{
    solve(gpu_state, initial_accumulate_, gl);
}

void ClothClothCollisionSolver::solve(const SceneGpuState& gpu_state,
                                      const AccumulateProgram& shader,
                                      QOpenGLFunctions_4_5_Core& gl) const
{
    const ClothGpuState& cloth_state = gpu_state.cloth_gpu_state();
    if (!cloth_state.has_multiple_garments()) {
        return;
    }

    // clear
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    clear_collision_correction_sum(gpu_state.collision_buffers().normal_correction_sum_buffer, gl);
    clear_collision_correction_sum(gpu_state.collision_buffers().contact_motion_delta_sum_buffer, gl);

    // accumulate
    const auto& collision_candidates = gpu_state.collision_buffers().cloth_cloth_vertex_face;
    const auto upper_vertex_offset = cloth_state.garment_states()[GarmentLayer::Upper].vertex_start_index;

    gl.glUseProgram(shader.program);
    gl.glProgramUniform1ui(shader.program, shader.max_candidates_loc, collision_candidates.max_pairs);
    gl.glProgramUniform1ui(shader.program, shader.upper_vertex_offset_loc, upper_vertex_offset);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_candidates.dispatch_size_buffer);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // apply
    const std::uint32_t vertex_count = gpu_state.cloth_gpu_state().element_counts().vertex;
    gl.glUseProgram(apply_program_);
    gl.glProgramUniform1ui(apply_program_, cloth_vertex_count_loc_, vertex_count);
    gl.glDispatchCompute(compute_group_count(vertex_count, apply_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothClothCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(apply_program_);
    gl.glDeleteProgram(initial_accumulate_.program);
    gl.glDeleteProgram(accumulate_.program);
    accumulate_ = {};
    initial_accumulate_ = {};
    apply_program_ = 0;
    cloth_vertex_count_loc_ = -1;
}
