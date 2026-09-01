#include "simulation/collision/CollisionDetector.h"

#include "gpu/scene/SceneGpuState.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

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

void CollisionDetector::detect(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const
{
    const ClothGpuState& cloth_state = gpu_state.cloth_gpu_state();
    const CollisionBuffers& collision = gpu_state.collision_buffers();

    clear_collision_candidate_counts(collision.cloth_vertex_body_face, gl);
    clear_collision_candidate_counts(collision.cloth_edge_body_edge, gl);
    clear_collision_candidate_counts(collision.cloth_face_body_vertex, gl);
    if (cloth_state.has_multiple_garments()) {
        clear_collision_candidate_counts(collision.cloth_cloth_vertex_face, gl);
    }

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    detect_cloth_vertex_body_face(gpu_state, gl);
    build_dispatch_size(CandidateKind::ClothVertexBodyFace, collision.cloth_vertex_body_face.max_pairs, gl);

    detect_cloth_edge_body_edge(gpu_state, gl);
    build_dispatch_size(CandidateKind::ClothEdgeBodyEdge, collision.cloth_edge_body_edge.max_pairs, gl);

    detect_cloth_face_body_vertex(gpu_state, gl);
    build_dispatch_size(CandidateKind::ClothFaceBodyVertex, collision.cloth_face_body_vertex.max_pairs, gl);

    if (cloth_state.has_multiple_garments()) {
        detect_cloth_cloth_vertex_face(gpu_state, gl);
        build_dispatch_size(CandidateKind::ClothClothVertexFace,
                            collision.cloth_cloth_vertex_face.max_pairs,
                            gl);
    }
}

void CollisionDetector::detect_prefit(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const
{
    const CollisionCandidateBuffers& candidates = gpu_state.collision_buffers().cloth_cloth_vertex_face;

    clear_collision_candidate_counts(candidates, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    detect_cloth_cloth_vertex_face(gpu_state, gl);
    build_dispatch_size(CandidateKind::ClothClothVertexFace, candidates.max_pairs, gl);
}

// Detection

void CollisionDetector::detect_cloth_vertex_body_face(const SceneGpuState& gpu_state,
                                                      QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& shader = cloth_vertex_body_face_;
    const auto& candidates = gpu_state.collision_buffers().cloth_vertex_body_face;
    const std::uint32_t vertex_count = gpu_state.cloth_gpu_state().element_counts().vertex;
    gl.glUseProgram(shader.program);
    gl.glProgramUniform1ui(shader.program, shader.item_count, vertex_count);
    gl.glProgramUniform1ui(shader.program, shader.max_candidates, candidates.max_pairs);
    gl.glDispatchCompute(compute_group_count(vertex_count, candidate_detect_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void CollisionDetector::detect_cloth_edge_body_edge(const SceneGpuState& gpu_state,
                                                    QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& shader = cloth_edge_body_edge_;
    const auto& candidates = gpu_state.collision_buffers().cloth_edge_body_edge;
    const std::uint32_t constraint_count = gpu_state.cloth_gpu_state().element_counts().stretch_constraint;
    gl.glUseProgram(shader.program);
    gl.glProgramUniform1ui(shader.program, shader.item_count, constraint_count);
    gl.glProgramUniform1ui(shader.program, shader.max_candidates, candidates.max_pairs);
    gl.glDispatchCompute(compute_group_count(constraint_count, candidate_detect_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void CollisionDetector::detect_cloth_face_body_vertex(const SceneGpuState& gpu_state,
                                                      QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& shader = cloth_face_body_vertex_;
    const auto& candidates = gpu_state.collision_buffers().cloth_face_body_vertex;
    const std::uint32_t triangle_count = gpu_state.cloth_gpu_state().element_counts().triangle;
    gl.glUseProgram(shader.program);
    gl.glProgramUniform1ui(shader.program, shader.item_count, triangle_count);
    gl.glProgramUniform1ui(shader.program, shader.max_candidates, candidates.max_pairs);
    gl.glDispatchCompute(compute_group_count(triangle_count, candidate_detect_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void CollisionDetector::detect_cloth_cloth_vertex_face(const SceneGpuState& gpu_state,
                                                       QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& shader = cloth_cloth_vertex_face_;
    const auto& candidates = gpu_state.collision_buffers().cloth_cloth_vertex_face;
    const auto& garment_states = gpu_state.cloth_gpu_state().garment_buffer_states();
    const GarmentBufferState& upper = garment_states[GarmentLayer::Upper];
    const GarmentBufferState& lower = garment_states[GarmentLayer::Lower];
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
