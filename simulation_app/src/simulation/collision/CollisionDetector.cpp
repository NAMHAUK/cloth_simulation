#include "simulation/collision/CollisionDetector.h"

#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace {
constexpr std::uint32_t candidate_detect_local_size = 128u;
}

// Initialization

void CollisionDetector::initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl)
{
    const std::filesystem::path collision_shader_dir = shader_dir / "collision";

    {
        const auto shader_path = collision_shader_dir / "cloth_body" / "cloth_vertex_body_face_detect.comp";
        auto& shader = cloth_vertex_body_face_;
        shader.program = load_compute_program(shader_path, gl);
        shader.item_count = require_uniform_location(shader.program, "uClothVertexCount", gl);
        shader.max_candidates = require_uniform_location(shader.program, "uMaxCandidateCount", gl);
    }

    {
        const auto shader_path = collision_shader_dir / "cloth_body" / "cloth_edge_body_edge_detect.comp";
        auto& shader = cloth_edge_body_edge_;
        shader.program = load_compute_program(shader_path, gl);
        shader.item_count = require_uniform_location(shader.program, "uEdgeCount", gl);
        shader.max_candidates = require_uniform_location(shader.program, "uMaxCandidateCount", gl);
    }

    {
        const auto shader_path = collision_shader_dir / "cloth_body" / "body_vertex_cloth_face_detect.comp";
        auto& shader = cloth_face_body_vertex_;
        shader.program = load_compute_program(shader_path, gl);
        shader.item_count = require_uniform_location(shader.program, "uTriangleCount", gl);
        shader.max_candidates = require_uniform_location(shader.program, "uMaxCandidateCount", gl);
    }

    {
        const auto shader_path = collision_shader_dir / "cloth_cloth" / "vertex_face_detect.comp";
        auto& shader = cloth_cloth_vertex_face_;
        shader.program = load_compute_program(shader_path, gl);
        shader.upper_vertex_offset = require_uniform_location(shader.program, "uUpperVertexOffset", gl);
        shader.upper_vertex_count = require_uniform_location(shader.program, "uUpperVertexCount", gl);
        shader.upper_bvh_root = require_uniform_location(shader.program, "uUpperBvhRoot", gl);
        shader.lower_vertex_offset = require_uniform_location(shader.program, "uLowerVertexOffset", gl);
        shader.lower_vertex_count = require_uniform_location(shader.program, "uLowerVertexCount", gl);
        shader.lower_bvh_root = require_uniform_location(shader.program, "uLowerBvhRoot", gl);
        shader.max_candidates = require_uniform_location(shader.program, "uMaxCandidateCount", gl);
    }

    {
        auto& shader = dispatch_size_;
        shader.program = load_compute_program(collision_shader_dir / "dispatch_size.comp", gl);
        shader.candidate_kind = require_uniform_location(shader.program, "uCandidateKind", gl);
        shader.max_candidates = require_uniform_location(shader.program, "uMaxCandidateCount", gl);
    }
}

// Detection

void CollisionDetector::detect(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_detect(views));

    clear_collision_candidate_counts(views.collision.cloth_vertex_body_face, gl);
    clear_collision_candidate_counts(views.collision.cloth_edge_body_edge, gl);
    clear_collision_candidate_counts(views.collision.cloth_face_body_vertex, gl);
    if (views.has_multiple_garments()) {
        clear_collision_candidate_counts(views.collision.cloth_cloth_vertex_face, gl);
    }

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    detect_cloth_vertex_body_face(views, gl);
    build_dispatch_size(CandidateKind::ClothVertexBodyFace,
                        views.collision.cloth_vertex_body_face.max_pairs,
                        gl);

    detect_cloth_edge_body_edge(views, gl);
    build_dispatch_size(CandidateKind::ClothEdgeBodyEdge, views.collision.cloth_edge_body_edge.max_pairs, gl);

    detect_cloth_face_body_vertex(views, gl);
    build_dispatch_size(CandidateKind::ClothFaceBodyVertex,
                        views.collision.cloth_face_body_vertex.max_pairs,
                        gl);

    if (views.has_multiple_garments()) {
        detect_cloth_cloth_vertex_face(views, gl);
        build_dispatch_size(CandidateKind::ClothClothVertexFace,
                            views.collision.cloth_cloth_vertex_face.max_pairs,
                            gl);
    }
}

void CollisionDetector::detect_prefit(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_detect_prefit(views));

    clear_collision_candidate_counts(views.collision.cloth_cloth_vertex_face, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    detect_cloth_cloth_vertex_face(views, gl);
    build_dispatch_size(CandidateKind::ClothClothVertexFace,
                        views.collision.cloth_cloth_vertex_face.max_pairs,
                        gl);
}

// Detection

void CollisionDetector::detect_cloth_vertex_body_face(const SimulationGpuView& views,
                                                      QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& shader = cloth_vertex_body_face_;
    const auto& candidates = views.collision.cloth_vertex_body_face;
    gl.glUseProgram(shader.program);
    gl.glProgramUniform1ui(shader.program, shader.item_count, views.cloth_topology.vertex_count);
    gl.glProgramUniform1ui(shader.program, shader.max_candidates, candidates.max_pairs);
    gl.glDispatchCompute(compute_group_count(views.cloth_topology.vertex_count, candidate_detect_local_size),
                         1,
                         1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void CollisionDetector::detect_cloth_edge_body_edge(const SimulationGpuView& views,
                                                    QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& shader = cloth_edge_body_edge_;
    const auto& candidates = views.collision.cloth_edge_body_edge;
    gl.glUseProgram(shader.program);
    gl.glProgramUniform1ui(shader.program, shader.item_count, views.stretch_constraints.constraint_count);
    gl.glProgramUniform1ui(shader.program, shader.max_candidates, candidates.max_pairs);
    gl.glDispatchCompute(
        compute_group_count(views.stretch_constraints.constraint_count, candidate_detect_local_size),
        1,
        1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void CollisionDetector::detect_cloth_face_body_vertex(const SimulationGpuView& views,
                                                      QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& shader = cloth_face_body_vertex_;
    const auto& candidates = views.collision.cloth_face_body_vertex;
    gl.glUseProgram(shader.program);
    gl.glProgramUniform1ui(shader.program, shader.item_count, views.cloth_topology.triangle_count);
    gl.glProgramUniform1ui(shader.program, shader.max_candidates, candidates.max_pairs);
    gl.glDispatchCompute(
        compute_group_count(views.cloth_topology.triangle_count, candidate_detect_local_size),
        1,
        1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void CollisionDetector::detect_cloth_cloth_vertex_face(const SimulationGpuView& views,
                                                       QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& shader = cloth_cloth_vertex_face_;
    const auto& candidates = views.collision.cloth_cloth_vertex_face;
    const GarmentBufferState& upper = views.garment_buffer_states[GarmentLayer::Upper];
    const GarmentBufferState& lower = views.garment_buffer_states[GarmentLayer::Lower];
    gl.glUseProgram(shader.program);
    gl.glProgramUniform1ui(shader.program, shader.max_candidates, candidates.max_pairs);
    gl.glProgramUniform1ui(shader.program, shader.upper_vertex_offset, upper.vertex_start_index);
    gl.glProgramUniform1ui(shader.program, shader.upper_vertex_count, upper.vertex_count);
    gl.glProgramUniform1ui(shader.program, shader.upper_bvh_root, upper.bvh_level_offsets.front());
    gl.glProgramUniform1ui(shader.program, shader.lower_vertex_offset, lower.vertex_start_index);
    gl.glProgramUniform1ui(shader.program, shader.lower_vertex_count, lower.vertex_count);
    gl.glProgramUniform1ui(shader.program, shader.lower_bvh_root, lower.bvh_level_offsets.front());

    const std::uint32_t query_vertex_count = upper.vertex_count + lower.vertex_count;
    gl.glDispatchCompute(compute_group_count(query_vertex_count, candidate_detect_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void CollisionDetector::build_dispatch_size(CandidateKind candidate_kind,
                                            std::uint32_t max_candidates,
                                            QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& shader = dispatch_size_;

    gl.glUseProgram(shader.program);
    gl.glProgramUniform1ui(shader.program, shader.candidate_kind, static_cast<GLuint>(candidate_kind));
    gl.glProgramUniform1ui(shader.program, shader.max_candidates, max_candidates);
    gl.glDispatchCompute(1, 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
}

// Validation

bool CollisionDetector::is_initialized() const
{
    return cloth_vertex_body_face_.program != 0 &&
           cloth_edge_body_edge_.program != 0 &&
           cloth_face_body_vertex_.program != 0 &&
           cloth_cloth_vertex_face_.program != 0 &&
           dispatch_size_.program != 0;
}

bool CollisionDetector::can_detect(const SimulationGpuView& views) const
{
    return is_initialized() &&
           views.cloth_topology.vertex_count != 0u &&
           views.cloth_topology.triangle_count != 0u &&
           views.body_topology.vertex_count != 0u &&
           views.body_topology.triangle_count != 0u &&
           views.stretch_constraints.constraint_count != 0 &&
           has_collision_candidate_capacity(views.collision.cloth_vertex_body_face) &&
           has_collision_candidate_capacity(views.collision.cloth_edge_body_edge) &&
           has_collision_candidate_capacity(views.collision.cloth_face_body_vertex) &&
           (!views.has_multiple_garments() || can_detect_prefit(views));
}

bool CollisionDetector::can_detect_prefit(const SimulationGpuView& views) const
{
    if (!is_initialized() ||
        !views.has_multiple_garments() ||
        views.cloth_topology.vertex_count == 0u ||
        !has_collision_candidate_capacity(views.collision.cloth_cloth_vertex_face)) {
        return false;
    }

    return std::all_of(views.garment_buffer_states.begin(),
                       views.garment_buffer_states.end(),
                       [&views](const GarmentBufferState& garment_state) {
                           return is_valid_buffer_access(garment_state.vertex_start_index,
                                                         garment_state.vertex_count,
                                                         views.cloth_topology.vertex_count) &&
                                  !garment_state.bvh_level_offsets.empty();
                       });
}

// Release

void CollisionDetector::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(cloth_vertex_body_face_.program);
    gl.glDeleteProgram(cloth_edge_body_edge_.program);
    gl.glDeleteProgram(cloth_face_body_vertex_.program);
    gl.glDeleteProgram(cloth_cloth_vertex_face_.program);
    gl.glDeleteProgram(dispatch_size_.program);

    cloth_vertex_body_face_ = {};
    cloth_edge_body_edge_ = {};
    cloth_face_body_vertex_ = {};
    cloth_cloth_vertex_face_ = {};
    dispatch_size_ = {};
}
