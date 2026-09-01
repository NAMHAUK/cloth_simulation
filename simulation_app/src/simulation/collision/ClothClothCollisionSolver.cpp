#include "simulation/collision/ClothClothCollisionSolver.h"

#include "gpu/scene/SceneGpuState.h"
#include "simulation/SimulationParams.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <stdexcept>

namespace {
constexpr std::uint32_t apply_local_size = 128u;
constexpr std::uint32_t body_triangle_index_build_local_size = 128u;

void clear_normal_correction_sums(const CollisionBuffers& buffers, QOpenGLFunctions_4_5_Core& gl)
{
    clear_collision_correction_sum(buffers.normal_correction_sum_buffer, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
}
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
    accumulate_.program = load_compute_program(collision_dir / "vertex_face_accumulate.comp", gl);
    initial_accumulate_.program = load_compute_program(collision_dir / "initial_layer_accumulate.comp", gl);
    body_triangle_index_build_.program =
        load_compute_program(collision_dir / "body_triangle_index_build.comp", gl);
    apply_.program = load_compute_program(collision_dir / "apply.comp", gl);

    accumulate_.max_candidates_loc = require_uniform_location(accumulate_.program, "uMaxCandidateCount", gl);
    accumulate_.body_triangle_count_loc =
        require_uniform_location(accumulate_.program, "uBodyTriangleCount", gl);
    accumulate_.upper_vertex_offset_loc =
        require_uniform_location(accumulate_.program, "uUpperVertexOffset", gl);
    initial_accumulate_.max_candidates_loc =
        require_uniform_location(initial_accumulate_.program, "uMaxCandidateCount", gl);
    initial_accumulate_.upper_vertex_offset_loc =
        require_uniform_location(initial_accumulate_.program, "uUpperVertexOffset", gl);
    body_triangle_index_build_.vertex_count_loc =
        require_uniform_location(body_triangle_index_build_.program, "uVertexCount", gl);
    body_triangle_index_build_.arm_triangle_ranges_loc =
        require_uniform_location(body_triangle_index_build_.program, "uArmTriangleRanges", gl);
    apply_.vertex_count_loc = require_uniform_location(apply_.program, "uVertexCount", gl);

    const GLint accumulate_collision_thickness_loc =
        require_uniform_location(accumulate_.program, "uCollisionThickness", gl);
    const GLint accumulate_collision_stiffness_loc =
        require_uniform_location(accumulate_.program, "uCollisionStiffness", gl);
    const GLint initial_collision_thickness_loc =
        require_uniform_location(initial_accumulate_.program, "uCollisionThickness", gl);
    const GLint initial_collision_stiffness_loc =
        require_uniform_location(initial_accumulate_.program, "uCollisionStiffness", gl);
    const GLint initial_search_radius_squared_loc =
        require_uniform_location(initial_accumulate_.program, "uSearchRadiusSquared", gl);
    const GLint body_search_radius_squared_loc =
        require_uniform_location(body_triangle_index_build_.program, "uSearchRadiusSquared", gl);
    const GLint max_correction_loc = require_uniform_location(apply_.program, "uMaxCorrectionLength", gl);

    gl.glProgramUniform1f(accumulate_.program, accumulate_collision_thickness_loc, collision_thickness_);
    gl.glProgramUniform1f(accumulate_.program, accumulate_collision_stiffness_loc, collision_stiffness_);
    gl.glProgramUniform1f(initial_accumulate_.program, initial_collision_thickness_loc, collision_thickness_);
    gl.glProgramUniform1f(initial_accumulate_.program, initial_collision_stiffness_loc, collision_stiffness_);
    gl.glProgramUniform1f(initial_accumulate_.program,
                          initial_search_radius_squared_loc,
                          surface_search_radius_ * surface_search_radius_);
    gl.glProgramUniform1f(body_triangle_index_build_.program,
                          body_search_radius_squared_loc,
                          surface_search_radius_ * surface_search_radius_);
    gl.glProgramUniform1f(apply_.program, max_correction_loc, max_correction_length_);
}

void ClothClothCollisionSolver::update_body_surface_mapping(const SceneGpuState& gpu_state,
                                                            QOpenGLFunctions_4_5_Core& gl) const
{
    const ClothGpuState& cloth_state = gpu_state.cloth_gpu_state();
    if (!cloth_state.has_multiple_garments()) {
        return;
    }

    const std::uint32_t vertex_count = cloth_state.element_counts().vertex;

    gl.glUseProgram(body_triangle_index_build_.program);
    gl.glProgramUniform1ui(body_triangle_index_build_.program,
                           body_triangle_index_build_.vertex_count_loc,
                           vertex_count);
    const glm::uvec4 arm_ranges = gpu_state.character_gpu_state().body_arm_triangle_ranges();
    gl.glProgramUniform4ui(body_triangle_index_build_.program,
                           body_triangle_index_build_.arm_triangle_ranges_loc,
                           arm_ranges.x,
                           arm_ranges.y,
                           arm_ranges.z,
                           arm_ranges.w);
    gl.glDispatchCompute(compute_group_count(vertex_count, body_triangle_index_build_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothClothCollisionSolver::solve(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const
{
    const ClothGpuState& cloth_state = gpu_state.cloth_gpu_state();
    if (!cloth_state.has_multiple_garments()) {
        return;
    }

    const CollisionBuffers& collision = gpu_state.collision_buffers();
    const auto& collision_candidates = collision.cloth_cloth_vertex_face;
    const std::uint32_t upper_vertex_offset =
        cloth_state.garment_buffer_states()[GarmentLayer::Upper].vertex_start_index;
    clear_normal_correction_sums(collision, gl);

    gl.glUseProgram(accumulate_.program);
    gl.glProgramUniform1ui(accumulate_.program,
                           accumulate_.max_candidates_loc,
                           collision_candidates.max_pairs);
    gl.glProgramUniform1ui(accumulate_.program,
                           accumulate_.body_triangle_count_loc,
                           gpu_state.character_gpu_state().triangle_count());
    gl.glProgramUniform1ui(accumulate_.program, accumulate_.upper_vertex_offset_loc, upper_vertex_offset);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_candidates.dispatch_size_buffer);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    apply_corrections(gpu_state, gl);
}

void ClothClothCollisionSolver::solve_initial(const SceneGpuState& gpu_state,
                                              QOpenGLFunctions_4_5_Core& gl) const
{
    const ClothGpuState& cloth_state = gpu_state.cloth_gpu_state();
    if (!cloth_state.has_multiple_garments()) {
        return;
    }

    const CollisionBuffers& collision = gpu_state.collision_buffers();
    const auto& collision_candidates = collision.cloth_cloth_vertex_face;
    const std::uint32_t upper_vertex_offset =
        cloth_state.garment_buffer_states()[GarmentLayer::Upper].vertex_start_index;
    clear_normal_correction_sums(collision, gl);

    gl.glUseProgram(initial_accumulate_.program);
    gl.glProgramUniform1ui(initial_accumulate_.program,
                           initial_accumulate_.max_candidates_loc,
                           collision_candidates.max_pairs);
    gl.glProgramUniform1ui(initial_accumulate_.program,
                           initial_accumulate_.upper_vertex_offset_loc,
                           upper_vertex_offset);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_candidates.dispatch_size_buffer);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    apply_corrections(gpu_state, gl);
}

void ClothClothCollisionSolver::apply_corrections(const SceneGpuState& gpu_state,
                                                  QOpenGLFunctions_4_5_Core& gl) const
{
    const std::uint32_t vertex_count = gpu_state.cloth_gpu_state().element_counts().vertex;
    gl.glUseProgram(apply_.program);
    gl.glProgramUniform1ui(apply_.program, apply_.vertex_count_loc, vertex_count);
    gl.glDispatchCompute(compute_group_count(vertex_count, apply_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothClothCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(apply_.program);
    gl.glDeleteProgram(body_triangle_index_build_.program);
    gl.glDeleteProgram(initial_accumulate_.program);
    gl.glDeleteProgram(accumulate_.program);
    accumulate_ = {};
    initial_accumulate_ = {};
    body_triangle_index_build_ = {};
    apply_ = {};
}
