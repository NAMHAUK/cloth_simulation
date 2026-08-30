#include "simulation/collision/ClothClothCollisionSolver.h"

#include "simulation/SimulationParams.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace {
constexpr std::uint32_t apply_local_size = 128u;
constexpr std::uint32_t body_triangle_index_build_local_size = 128u;

bool has_valid_common_solve_metadata(const SimulationGpuView& views)
{
    if (views.cloth_topology.vertex_count == 0u || views.cloth_topology.triangle_count == 0u) {
        return false;
    }
    if (!views.has_multiple_garments()) {
        return true;
    }

    return views.body_topology.triangle_count != 0u &&
           has_collision_candidate_capacity(views.collision.cloth_cloth_vertex_face) &&
           std::all_of(views.garment_buffer_states.begin(),
                       views.garment_buffer_states.end(),
                       [&views](const GarmentBufferState& garment_state) {
                           return is_valid_buffer_access(garment_state.vertex_start_index,
                                                         garment_state.vertex_count,
                                                         views.cloth_topology.vertex_count) &&
                                  !garment_state.bvh_level_offsets.empty();
                       });
}

void clear_normal_correction_sums(const CollisionBufferView& buffers, QOpenGLFunctions_4_5_Core& gl)
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

bool ClothClothCollisionSolver::is_initialized() const
{
    return accumulate_.program != 0 &&
           initial_accumulate_.program != 0 &&
           body_triangle_index_build_.program != 0 &&
           apply_.program != 0;
}

void ClothClothCollisionSolver::initialize(const std::filesystem::path& shader_dir,
                                           QOpenGLFunctions_4_5_Core& gl)
{
    const std::filesystem::path cloth_cloth_shader_dir = shader_dir / "collision" / "cloth_cloth";
    accumulate_.program = load_compute_program(cloth_cloth_shader_dir / "vertex_face_accumulate.comp", gl);
    initial_accumulate_.program =
        load_compute_program(cloth_cloth_shader_dir / "initial_layer_accumulate.comp", gl);
    body_triangle_index_build_.program =
        load_compute_program(cloth_cloth_shader_dir / "body_triangle_index_build.comp", gl);
    apply_.program = load_compute_program(cloth_cloth_shader_dir / "apply.comp", gl);
    accumulate_.max_candidates = require_uniform_location(accumulate_.program, "uMaxCandidateCount", gl);
    accumulate_.collision_thickness =
        require_uniform_location(accumulate_.program, "uCollisionThickness", gl);
    accumulate_.collision_stiffness =
        require_uniform_location(accumulate_.program, "uCollisionStiffness", gl);
    accumulate_.body_triangle_count = require_uniform_location(accumulate_.program, "uBodyTriangleCount", gl);
    accumulate_.upper_vertex_offset = require_uniform_location(accumulate_.program, "uUpperVertexOffset", gl);
    initial_accumulate_.max_candidates =
        require_uniform_location(initial_accumulate_.program, "uMaxCandidateCount", gl);
    initial_accumulate_.collision_thickness =
        require_uniform_location(initial_accumulate_.program, "uCollisionThickness", gl);
    initial_accumulate_.collision_stiffness =
        require_uniform_location(initial_accumulate_.program, "uCollisionStiffness", gl);
    initial_accumulate_.search_radius_squared =
        require_uniform_location(initial_accumulate_.program, "uSearchRadiusSquared", gl);
    initial_accumulate_.upper_vertex_offset =
        require_uniform_location(initial_accumulate_.program, "uUpperVertexOffset", gl);
    body_triangle_index_build_.vertex_count =
        require_uniform_location(body_triangle_index_build_.program, "uVertexCount", gl);
    body_triangle_index_build_.search_radius_squared =
        require_uniform_location(body_triangle_index_build_.program, "uSearchRadiusSquared", gl);
    body_triangle_index_build_.arm_triangle_ranges =
        require_uniform_location(body_triangle_index_build_.program, "uArmTriangleRanges", gl);
    apply_.vertex_count = require_uniform_location(apply_.program, "uVertexCount", gl);
    apply_.max_correction = require_uniform_location(apply_.program, "uMaxCorrectionLength", gl);
}

bool ClothClothCollisionSolver::can_solve(const SimulationGpuView& views) const
{
    return is_initialized() &&
           has_valid_common_solve_metadata(views) &&
           collision_thickness_ > 0.0f &&
           collision_stiffness_ > 0.0f &&
           max_correction_length_ > 0.0f;
}

bool ClothClothCollisionSolver::can_solve_initial(const SimulationGpuView& views) const
{
    return is_initialized() &&
           has_valid_common_solve_metadata(views) &&
           collision_thickness_ > 0.0f &&
           collision_stiffness_ > 0.0f &&
           max_correction_length_ > 0.0f &&
           surface_search_radius_ > 0.0f;
}

bool ClothClothCollisionSolver::can_update_body_surface_mapping(const SimulationGpuView& views) const
{
    return is_initialized() && has_valid_common_solve_metadata(views) && surface_search_radius_ > 0.0f;
}

void ClothClothCollisionSolver::update_body_surface_mapping(const SimulationGpuView& views,
                                                            QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_update_body_surface_mapping(views));
    if (!views.has_multiple_garments()) {
        return;
    }

    gl.glUseProgram(body_triangle_index_build_.program);
    gl.glProgramUniform1ui(body_triangle_index_build_.program,
                           body_triangle_index_build_.vertex_count,
                           views.cloth_topology.vertex_count);
    gl.glProgramUniform1f(body_triangle_index_build_.program,
                          body_triangle_index_build_.search_radius_squared,
                          surface_search_radius_ * surface_search_radius_);
    const glm::uvec4 arm_ranges = views.body_arm_triangle_ranges;
    gl.glProgramUniform4ui(body_triangle_index_build_.program,
                           body_triangle_index_build_.arm_triangle_ranges,
                           arm_ranges.x,
                           arm_ranges.y,
                           arm_ranges.z,
                           arm_ranges.w);
    gl.glDispatchCompute(
        compute_group_count(views.cloth_topology.vertex_count, body_triangle_index_build_local_size),
        1,
        1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothClothCollisionSolver::solve(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(views));
    if (!views.has_multiple_garments()) {
        return;
    }

    const CollisionCandidateBufferView& collision_candidates = views.collision.cloth_cloth_vertex_face;
    const std::uint32_t upper_vertex_offset =
        views.garment_buffer_states[GarmentLayer::Upper].vertex_start_index;
    clear_normal_correction_sums(views.collision, gl);

    gl.glUseProgram(accumulate_.program);
    gl.glProgramUniform1ui(accumulate_.program, accumulate_.max_candidates, collision_candidates.max_pairs);
    gl.glProgramUniform1f(accumulate_.program, accumulate_.collision_thickness, collision_thickness_);
    gl.glProgramUniform1f(accumulate_.program, accumulate_.collision_stiffness, collision_stiffness_);
    gl.glProgramUniform1ui(accumulate_.program,
                           accumulate_.body_triangle_count,
                           views.body_topology.triangle_count);
    gl.glProgramUniform1ui(accumulate_.program, accumulate_.upper_vertex_offset, upper_vertex_offset);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_candidates.dispatch_size_buffer);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    apply_corrections(views, gl);
}

void ClothClothCollisionSolver::solve_initial(const SimulationGpuView& views,
                                              QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve_initial(views));
    if (!views.has_multiple_garments()) {
        return;
    }

    const CollisionCandidateBufferView& collision_candidates = views.collision.cloth_cloth_vertex_face;
    const std::uint32_t upper_vertex_offset =
        views.garment_buffer_states[GarmentLayer::Upper].vertex_start_index;
    clear_normal_correction_sums(views.collision, gl);

    gl.glUseProgram(initial_accumulate_.program);
    gl.glProgramUniform1ui(initial_accumulate_.program,
                           initial_accumulate_.max_candidates,
                           collision_candidates.max_pairs);
    gl.glProgramUniform1f(initial_accumulate_.program,
                          initial_accumulate_.collision_thickness,
                          collision_thickness_);
    gl.glProgramUniform1f(initial_accumulate_.program,
                          initial_accumulate_.collision_stiffness,
                          collision_stiffness_);
    gl.glProgramUniform1f(initial_accumulate_.program,
                          initial_accumulate_.search_radius_squared,
                          surface_search_radius_ * surface_search_radius_);
    gl.glProgramUniform1ui(initial_accumulate_.program,
                           initial_accumulate_.upper_vertex_offset,
                           upper_vertex_offset);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, collision_candidates.dispatch_size_buffer);
    gl.glDispatchComputeIndirect(0);
    gl.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    apply_corrections(views, gl);
}

void ClothClothCollisionSolver::apply_corrections(const SimulationGpuView& views,
                                                  QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(apply_.program);
    gl.glProgramUniform1ui(apply_.program, apply_.vertex_count, views.cloth_topology.vertex_count);
    gl.glProgramUniform1f(apply_.program, apply_.max_correction, max_correction_length_);
    gl.glDispatchCompute(compute_group_count(views.cloth_topology.vertex_count, apply_local_size), 1, 1);
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
