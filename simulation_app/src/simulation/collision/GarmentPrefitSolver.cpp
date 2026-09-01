#include "simulation/collision/GarmentPrefitSolver.h"

#include "gpu/cloth/ClothGpuState.h"
#include "simulation/SimulationParams.h"
#include "utils/ShaderUtils.h"

#include <stdexcept>

namespace {
constexpr std::uint32_t local_size = 128;
}

GarmentPrefitSolver::GarmentPrefitSolver(const PrefitParams& params)
    : search_radius_(params.surface_search_radius),
      pushout_margin_(params.pushout_margin)
{}

void GarmentPrefitSolver::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_dir / "cloth" / "setup" / "garment_prefit.comp", gl);
    vertex_offset_loc_ = require_uniform_location(program_, "uVertexOffset", gl);
    vertex_count_loc_ = require_uniform_location(program_, "uVertexCount", gl);

    const GLint search_radius_squared_loc = require_uniform_location(program_, "uSearchRadiusSquared", gl);
    const GLint pushout_margin_loc = require_uniform_location(program_, "uPushoutMargin", gl);

    gl.glProgramUniform1f(program_, search_radius_squared_loc, search_radius_ * search_radius_);
    gl.glProgramUniform1f(program_, pushout_margin_loc, pushout_margin_);
}

void GarmentPrefitSolver::solve(const ClothGpuState& cloth_state,
                                GarmentLayer layer,
                                QOpenGLFunctions_4_5_Core& gl) const
{
    const GarmentBufferState& garment_state = cloth_state.garment_buffer_states()[layer];

    gl.glUseProgram(program_);

    gl.glProgramUniform1ui(program_, vertex_offset_loc_, garment_state.vertex_start_index);
    gl.glProgramUniform1ui(program_, vertex_count_loc_, garment_state.vertex_count);

    gl.glDispatchCompute(compute_group_count(garment_state.vertex_count, local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void GarmentPrefitSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    vertex_offset_loc_ = -1;
    vertex_count_loc_ = -1;
}
