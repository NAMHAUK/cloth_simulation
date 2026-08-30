#include "simulation/collision/GarmentPrefitSolver.h"

#include "gpu/cloth/ClothGpuState.h"
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

bool GarmentPrefitSolver::can_solve(const ClothGpuState& cloth_state, GarmentLayer layer) const
{
    const GarmentBufferState& garment_state = cloth_state.garment_buffer_states()[layer];
    return is_initialized() &&
           is_valid_buffer_access(garment_state.vertex_start_index,
                                  garment_state.vertex_count,
                                  cloth_state.element_counts().vertex) &&
           search_radius_ > 0.0f &&
           pushout_margin_ > 0.0f;
}

void GarmentPrefitSolver::solve(const ClothGpuState& cloth_state,
                                GarmentLayer layer,
                                QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(cloth_state, layer));

    const GarmentBufferState& garment_state = cloth_state.garment_buffer_states()[layer];

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
