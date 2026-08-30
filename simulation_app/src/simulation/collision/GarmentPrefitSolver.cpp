#include "simulation/collision/GarmentPrefitSolver.h"

#include "gpu/scene/SimulationGpuView.h"
#include "simulation/SimulationParams.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <stdexcept>

namespace {
constexpr std::uint32_t garment_prefit_local_size = 128;
}

GarmentPrefitSolver::GarmentPrefitSolver(const PrefitParams& params)
    : search_radius_(params.surface_search_radius),
      pushout_margin_(params.pushout_margin)
{}

bool GarmentPrefitSolver::is_initialized() const
{
    return program_ != 0;
}

void GarmentPrefitSolver::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_dir / "cloth" / "setup" / "garment_prefit.comp", gl);
    vertex_offset_location_ = require_uniform_location(program_, "uVertexOffset", gl);
    vertex_count_location_ = require_uniform_location(program_, "uVertexCount", gl);
    search_radius_squared_location_ = require_uniform_location(program_, "uSearchRadiusSquared", gl);
    pushout_margin_location_ = require_uniform_location(program_, "uPushoutMargin", gl);
}

bool GarmentPrefitSolver::can_solve(const SimulationGpuView& views, GarmentLayer layer) const
{
    const GarmentBufferState& garment_state = views.garment_buffer_states[layer];
    return is_initialized() &&
           is_valid_motion_view(views.cloth_motion) &&
           is_valid_buffer_access(garment_state.vertex_start_index,
                                  garment_state.vertex_count,
                                  views.cloth_motion.vertex_count) &&
           is_valid_body_triangle_resource(views.body_triangles) &&
           is_valid_bvh_buffer_view(views.body_triangle_bvh) &&
           search_radius_ > 0.0f &&
           pushout_margin_ > 0.0f;
}

void GarmentPrefitSolver::solve(const SimulationGpuView& views,
                                GarmentLayer layer,
                                QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(views, layer));

    const GarmentBufferState& garment_state = views.garment_buffer_states[layer];

    gl.glUseProgram(program_);

    gl.glProgramUniform1ui(program_, vertex_offset_location_, garment_state.vertex_start_index);
    gl.glProgramUniform1ui(program_, vertex_count_location_, garment_state.vertex_count);
    gl.glProgramUniform1f(program_, search_radius_squared_location_, search_radius_ * search_radius_);
    gl.glProgramUniform1f(program_, pushout_margin_location_, pushout_margin_);

    gl.glDispatchCompute(compute_group_count(garment_state.vertex_count, garment_prefit_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void GarmentPrefitSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    vertex_offset_location_ = -1;
    vertex_count_location_ = -1;
    search_radius_squared_location_ = -1;
    pushout_margin_location_ = -1;
}
