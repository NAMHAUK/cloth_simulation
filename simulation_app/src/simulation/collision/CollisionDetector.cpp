#include "simulation/collision/CollisionDetector.h"

#include "gpu/scene/SceneGpuState.h"
#include "simulation/SimulationParams.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <array>
#include <stdexcept>

namespace {
constexpr std::uint32_t candidate_detect_local_size = 128u;
}

// Initialization

CollisionDetector::CollisionDetector(const ClothCollisionParams& params)
    : cloth_detection_distance_(params.detection_distance)
{}

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
        const auto shader_path = collision_shader_dir / "cloth_cloth" / "vertex_face_detect.comp";
        auto& shader = cloth_cloth_vertex_face_;
        shader.program = load_compute_program(shader_path, gl);
        shader.vertex_offsets = require_uniform_location(shader.program, "uVertexOffsets[0]", gl);
        shader.vertex_counts = require_uniform_location(shader.program, "uVertexCounts[0]", gl);
        shader.bvh_roots = require_uniform_location(shader.program, "uBvhRoots[0]", gl);
        shader.triangle_offsets = require_uniform_location(shader.program, "uTriangleOffsets[0]", gl);
        shader.masks_per_vertex = require_uniform_location(shader.program, "uMasksPerVertex[0]", gl);
        shader.exclusion_offsets = require_uniform_location(shader.program, "uExclusionOffsets[0]", gl);
        shader.max_candidates = require_uniform_location(shader.program, "uMaxCandidateCount", gl);
    }

    {
        auto& shader = dispatch_size_;
        shader.program = load_compute_program(collision_shader_dir / "dispatch_size.comp", gl);
        shader.candidate_kind = require_uniform_location(shader.program, "uCandidateKind", gl);
        shader.max_candidates = require_uniform_location(shader.program, "uMaxCandidateCount", gl);
    }

    {
        auto& shader = cloth_cloth_edge_edge_;
        shader.program = load_compute_program(collision_shader_dir / "cloth_cloth" / "edge_edge_detect.comp", gl);
        shader.edge_offset = require_uniform_location(shader.program, "uEdgeOffset", gl);
        shader.edge_count = require_uniform_location(shader.program, "uEdgeCount", gl);
        shader.edge_exclusion_offset_loc = require_uniform_location(shader.program, "uEdgeExclusionOffset", gl);
        shader.target_bvh_root = require_uniform_location(shader.program, "uTargetBvhRoot", gl);
        shader.is_self_collision = require_uniform_location(shader.program, "uIsSelfCollision", gl);
        shader.max_candidates = require_uniform_location(shader.program, "uMaxCandidateCount", gl);
        const GLint distance_loc = require_uniform_location(shader.program, "uDetectionDistance", gl);
        gl.glProgramUniform1f(shader.program, distance_loc, cloth_detection_distance_);
    }
}

// Detection

void CollisionDetector::detect(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const
{
    const CollisionBuffers& collision = gpu_state.collision_buffers();

    gl.glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    clear_collision_candidate_counts(collision.cloth_vertex_body_face, gl);
    clear_collision_candidate_counts(collision.cloth_edge_body_edge, gl);
    clear_collision_candidate_counts(collision.cloth_cloth_vertex_face, gl);
    clear_collision_candidate_counts(collision.cloth_cloth_edge_edge, gl);

    detect_cloth_vertex_body_face(gpu_state, gl);
    detect_cloth_edge_body_edge(gpu_state, gl);
    detect_cloth_cloth_vertex_face(gpu_state, gl);
    detect_cloth_cloth_edge_edge(gpu_state, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    build_dispatch_size(CandidateKind::ClothVertexBodyFace, collision.cloth_vertex_body_face.max_pairs, gl);
    build_dispatch_size(CandidateKind::ClothEdgeBodyEdge, collision.cloth_edge_body_edge.max_pairs, gl);
    build_dispatch_size(CandidateKind::ClothClothVertexFace, collision.cloth_cloth_vertex_face.max_pairs, gl);
    build_dispatch_size(CandidateKind::ClothClothEdgeEdge, collision.cloth_cloth_edge_edge.max_pairs, gl);
    gl.glMemoryBarrier(GL_COMMAND_BARRIER_BIT);
}

void CollisionDetector::detect_prefit(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const
{
    const CollisionCandidateBuffers& candidates = gpu_state.collision_buffers().cloth_cloth_vertex_face;

    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    clear_collision_candidate_counts(candidates, gl);
    detect_cloth_cloth_vertex_face(gpu_state, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    build_dispatch_size(CandidateKind::ClothClothVertexFace, candidates.max_pairs, gl);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
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
}

void CollisionDetector::detect_cloth_cloth_vertex_face(const SceneGpuState& gpu_state,
                                                       QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& shader = cloth_cloth_vertex_face_;
    const auto& candidates = gpu_state.collision_buffers().cloth_cloth_vertex_face;

    const auto& garment_states = gpu_state.cloth_gpu_state().garment_states();
    const GarmentBufferState& upper = garment_states[GarmentLayer::Upper];
    const GarmentBufferState& lower = garment_states[GarmentLayer::Lower];
    const std::uint32_t vertex_count = upper.vertex_count + lower.vertex_count;

    const std::array<GLuint, 2> vertex_offsets{lower.vertex_start_index, upper.vertex_start_index};
    const std::array<GLuint, 2> vertex_counts{lower.vertex_count, upper.vertex_count};
    const std::array<GLuint, 2> bvh_roots{
        lower.vertex_count > 0u ? lower.triangle_bvh_level_offsets.front() : 0u,
        upper.vertex_count > 0u ? upper.triangle_bvh_level_offsets.front() : 0u,
    };
    const std::array<GLuint, 2> triangle_offsets{lower.triangle_start_index, upper.triangle_start_index};
    const std::array<GLuint, 2> masks_per_vertex{(lower.triangle_count + 31u) / 32u, (upper.triangle_count + 31u) / 32u};
    const std::array<GLuint, 2> exclusion_offsets{lower.vertex_face_exclusion_offset, upper.vertex_face_exclusion_offset};

    gl.glUseProgram(shader.program);
    gl.glProgramUniform1ui(shader.program, shader.max_candidates, candidates.max_pairs);

    gl.glProgramUniform1uiv(shader.program, shader.vertex_offsets, 2, vertex_offsets.data());
    gl.glProgramUniform1uiv(shader.program, shader.vertex_counts, 2, vertex_counts.data());
    gl.glProgramUniform1uiv(shader.program, shader.bvh_roots, 2, bvh_roots.data());
    gl.glProgramUniform1uiv(shader.program, shader.triangle_offsets, 2, triangle_offsets.data());
    gl.glProgramUniform1uiv(shader.program, shader.masks_per_vertex, 2, masks_per_vertex.data());
    gl.glProgramUniform1uiv(shader.program, shader.exclusion_offsets, 2, exclusion_offsets.data());

    gl.glDispatchCompute(compute_group_count(vertex_count, candidate_detect_local_size), 1, 1);
}

void CollisionDetector::detect_cloth_cloth_edge_edge(const SceneGpuState& gpu_state,
                                                     QOpenGLFunctions_4_5_Core& gl) const
{
    const auto& shader = cloth_cloth_edge_edge_;
    const auto& candidates = gpu_state.collision_buffers().cloth_cloth_edge_edge;
    const auto& garments = gpu_state.cloth_gpu_state().garment_states();
    const auto& lower = garments[GarmentLayer::Lower];
    const auto& upper = garments[GarmentLayer::Upper];

    gl.glUseProgram(shader.program);
    gl.glProgramUniform1ui(shader.program, shader.max_candidates, candidates.max_pairs);
    detect_edges(lower, upper, false, gl);
    detect_edges(lower, lower, true, gl);
    detect_edges(upper, upper, true, gl);
}

void CollisionDetector::detect_edges(const GarmentBufferState& source,
                                     const GarmentBufferState& target,
                                     bool is_self_collision,
                                     QOpenGLFunctions_4_5_Core& gl) const
{
    if (source.edge_count == 0u || target.edge_count == 0u) {
        return;
    }
    const auto& shader = cloth_cloth_edge_edge_;
    gl.glProgramUniform1ui(shader.program, shader.edge_offset, source.edge_start_index);
    gl.glProgramUniform1ui(shader.program, shader.edge_count, source.edge_count);
    gl.glProgramUniform1ui(shader.program, shader.edge_exclusion_offset_loc, source.edge_exclusion_offset);
    gl.glProgramUniform1ui(shader.program, shader.target_bvh_root, target.edge_bvh_level_offsets.front());
    gl.glProgramUniform1i(shader.program, shader.is_self_collision, is_self_collision);
    gl.glDispatchCompute(compute_group_count(source.edge_count, candidate_detect_local_size), 1, 1);
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
}

// Release

void CollisionDetector::release(QOpenGLFunctions_4_5_Core& gl)
{
    gl.glDeleteProgram(cloth_vertex_body_face_.program);
    gl.glDeleteProgram(cloth_edge_body_edge_.program);
    gl.glDeleteProgram(cloth_cloth_vertex_face_.program);
    gl.glDeleteProgram(cloth_cloth_edge_edge_.program);
    gl.glDeleteProgram(dispatch_size_.program);

    cloth_vertex_body_face_ = {};
    cloth_edge_body_edge_ = {};
    cloth_cloth_vertex_face_ = {};
    cloth_cloth_edge_edge_ = {};
    dispatch_size_ = {};
}
