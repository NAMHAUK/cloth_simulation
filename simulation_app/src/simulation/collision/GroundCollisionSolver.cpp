#include "simulation/collision/GroundCollisionSolver.h"

#include "gpu/scene/SimulationGpuView.h"
#include "simulation/SimulationParams.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <iostream>

namespace {
constexpr GLuint current_positions_binding = 0;
constexpr GLuint previous_positions_binding = 1;
constexpr GLuint collision_pushouts_binding = 2;
constexpr GLuint cloth_cloth_pushouts_binding = 3;
constexpr GLuint contact_motion_deltas_binding = 4;
constexpr std::uint32_t ground_collision_local_size = 128;
}

GroundCollisionSolver::GroundCollisionSolver(const GroundCollisionParams& params)
    : floor_height_(params.height),
      static_friction_(params.static_friction),
      dynamic_friction_(params.dynamic_friction)
{}

bool GroundCollisionSolver::is_initialized() const
{
    return program_ != 0;
}

bool GroundCollisionSolver::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    program_ = load_compute_program(shader_dir / "collision" / "cloth_ground_collision.comp",
                                    "Ground collision",
                                    gl);
    if (program_ == 0) {
        return false;
    }

    vertex_count_location_ = gl.glGetUniformLocation(program_, "uVertexCount");
    floor_height_location_ = gl.glGetUniformLocation(program_, "uFloorHeight");
    static_friction_location_ = gl.glGetUniformLocation(program_, "uStaticFriction");
    dynamic_friction_location_ = gl.glGetUniformLocation(program_, "uDynamicFriction");

    if (vertex_count_location_ < 0 ||
        floor_height_location_ < 0 ||
        static_friction_location_ < 0 ||
        dynamic_friction_location_ < 0) {
        std::cerr << "Ground collision compute shader missing required uniforms.\n";
        release(gl);
        return false;
    }

    return true;
}

bool GroundCollisionSolver::can_solve(const SimulationGpuView& views) const
{
    return is_initialized() &&
           is_valid_motion_view(views.cloth_motion) &&
           is_valid_collision_pushout_view(views.cloth_collision_pushout) &&
           is_valid_contact_motion_view(views.cloth_contact_motion) &&
           views.cloth_motion.vertex_count == views.cloth_collision_pushout.vertex_count &&
           views.cloth_motion.vertex_count == views.cloth_contact_motion.vertex_count &&
           dynamic_friction_ >= 0.0f &&
           static_friction_ >= dynamic_friction_;
}

void GroundCollisionSolver::solve(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(views));

    const auto& motion_view = views.cloth_motion;
    const auto& collision_pushout_view = views.cloth_collision_pushout;
    const auto& contact_motion_view = views.cloth_contact_motion;

    // shader & GPU 연결
    gl.glUseProgram(program_);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        current_positions_binding,
                        motion_view.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        previous_positions_binding,
                        motion_view.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        collision_pushouts_binding,
                        collision_pushout_view.collision_pushout_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        cloth_cloth_pushouts_binding,
                        collision_pushout_view.cloth_cloth_pushout_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        contact_motion_deltas_binding,
                        contact_motion_view.contact_motion_delta_buffer);

    // shader에 값 전달
    gl.glProgramUniform1ui(program_, vertex_count_location_, motion_view.vertex_count);
    gl.glProgramUniform1f(program_, floor_height_location_, floor_height_);
    gl.glProgramUniform1f(program_, static_friction_location_, static_friction_);
    gl.glProgramUniform1f(program_, dynamic_friction_location_, dynamic_friction_);

    // shader가 바닥과 충돌 처리 (GPU에서 바로 업데이트)
    gl.glDispatchCompute(compute_group_count(motion_view.vertex_count, ground_collision_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void GroundCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(program_);

    program_ = 0;
    vertex_count_location_ = -1;
    floor_height_location_ = -1;
    static_friction_location_ = -1;
    dynamic_friction_location_ = -1;
}
