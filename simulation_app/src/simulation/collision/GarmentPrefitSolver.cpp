#include "simulation/collision/GarmentPrefitSolver.h"
#include "gpu/cloth/ClothGpuState.h"
#include "simulation/SimulationParams.h"
#include "utils/ShaderUtils.h"

namespace {
constexpr std::uint32_t local_size = 128;
}

GarmentPrefitSolver::GarmentPrefitSolver(const PrefitParams& params)
    : search_radius_(params.surface_search_radius),
      pushout_margin_(params.pushout_margin),
      max_correction_length_(params.max_correction_length)
{}

void GarmentPrefitSolver::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_dir / "cloth" / "setup" / "garment_prefit.comp", gl);
    vertex_count_loc_ = require_uniform_location(program_, "uVertexCount", gl);

    const GLint search_radius_squared_loc = require_uniform_location(program_, "uSearchRadiusSquared", gl);
    const GLint pushout_margin_loc = require_uniform_location(program_, "uPushoutMargin", gl);
    const GLint max_correction_length_loc = require_uniform_location(program_, "uMaxCorrectionLength", gl);

    gl.glProgramUniform1f(program_, search_radius_squared_loc, search_radius_ * search_radius_);
    gl.glProgramUniform1f(program_, pushout_margin_loc, pushout_margin_);
    gl.glProgramUniform1f(program_, max_correction_length_loc, max_correction_length_);
}

void GarmentPrefitSolver::solve(const ClothGpuState& cloth_state, QOpenGLFunctions_4_5_Core& gl) const
{
    const std::uint32_t vertex_count = cloth_state.element_counts().vertex;

    gl.glUseProgram(program_);

    gl.glProgramUniform1ui(program_, vertex_count_loc_, vertex_count);

    gl.glDispatchCompute(compute_group_count(vertex_count, local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void GarmentPrefitSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    vertex_count_loc_ = -1;
}
