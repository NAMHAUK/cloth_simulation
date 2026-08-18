#include "simulation/collision/ClothClothCollisionDetector.h"

#include "gpu/scene/CollisionCandidateBuffers.h"
#include "simulation/SimulationParams.h"
#include "utils/BufferUtils.h"
#include "utils/ShaderUtils.h"

#include <cassert>
#include <stdexcept>

namespace {
constexpr std::uint32_t candidate_detect_local_size = 128u;
constexpr std::uint32_t candidate_accumulate_local_size = 128u;

namespace candidate_detect_binding {
constexpr GLuint cloth_current = 0;
constexpr GLuint cloth_previous = 1;
constexpr GLuint triangle_bounds = 2;
constexpr GLuint bvh_nodes = 3;
constexpr GLuint candidates = 4;
constexpr GLuint candidate_count = 5;
constexpr GLuint overflow_count = 6;
}

namespace dispatch_size_binding {
constexpr GLuint candidate_count = 0;
constexpr GLuint dispatch_size = 1;
}
}

ClothClothCollisionDetector::ClothClothCollisionDetector(const ClothCollisionParams& params)
    : initial_detection_distance_(params.initial_detection_distance),
      detection_distance_(params.detection_distance())
{}

bool ClothClothCollisionDetector::is_initialized() const
{
    return candidate_detect_.program != 0 && dispatch_size_.program != 0;
}

void ClothClothCollisionDetector::initialize(const std::filesystem::path& shader_dir,
                                             QOpenGLFunctions_4_5_Core& gl)
{
    const std::filesystem::path collision_shader_dir = shader_dir / "collision";
    candidate_detect_.program =
        load_compute_program(collision_shader_dir / "cloth_cloth_vertex_face_detect.comp",
                             "Cloth-cloth vertex-face candidate detection",
                             gl);
    dispatch_size_.program = load_compute_program(collision_shader_dir / "collision_dispatch_size.comp",
                                                  "Cloth-cloth candidate dispatch size",
                                                  gl);
    candidate_detect_.upper_vertex_offset =
        gl.glGetUniformLocation(candidate_detect_.program, "uUpperVertexOffset");
    candidate_detect_.upper_vertex_count =
        gl.glGetUniformLocation(candidate_detect_.program, "uUpperVertexCount");
    candidate_detect_.upper_bvh_root = gl.glGetUniformLocation(candidate_detect_.program, "uUpperBvhRoot");
    candidate_detect_.lower_vertex_offset =
        gl.glGetUniformLocation(candidate_detect_.program, "uLowerVertexOffset");
    candidate_detect_.lower_vertex_count =
        gl.glGetUniformLocation(candidate_detect_.program, "uLowerVertexCount");
    candidate_detect_.lower_bvh_root = gl.glGetUniformLocation(candidate_detect_.program, "uLowerBvhRoot");
    candidate_detect_.max_candidates =
        gl.glGetUniformLocation(candidate_detect_.program, "uMaxCandidateCount");
    dispatch_size_.max_candidates = gl.glGetUniformLocation(dispatch_size_.program, "uMaxCandidateCount");
    dispatch_size_.local_size = gl.glGetUniformLocation(dispatch_size_.program, "uLocalSize");

    if (candidate_detect_.upper_vertex_offset < 0 ||
        candidate_detect_.upper_vertex_count < 0 ||
        candidate_detect_.upper_bvh_root < 0 ||
        candidate_detect_.lower_vertex_offset < 0 ||
        candidate_detect_.lower_vertex_count < 0 ||
        candidate_detect_.lower_bvh_root < 0 ||
        candidate_detect_.max_candidates < 0 ||
        dispatch_size_.max_candidates < 0 ||
        dispatch_size_.local_size < 0) {
        throw std::runtime_error("Cloth-cloth candidate detection compute shader missing required uniforms.");
    }

    bounds_updater_.initialize(shader_dir, gl);
}

bool ClothClothCollisionDetector::can_detect(const SimulationGpuView& views) const
{
    if (!is_initialized() ||
        !is_valid_motion_view(views.cloth_motion) ||
        !is_valid_cloth_mesh_topology_resource(views.cloth_topology) ||
        !is_valid_bvh_buffer_view(views.cloth_bvh) ||
        views.cloth_topology.vertex_count != views.cloth_motion.vertex_count) {
        return false;
    }

    const std::uint32_t garment_count = active_garment_count(views.garment_buffer_states);
    if (garment_count < 2u) {
        return true;
    }

    std::uint32_t required_capacity = 0;
    return CollisionCandidateBuffers::calculate_cloth_cloth_candidate_capacity(
               views.cloth_motion.vertex_count,
               garment_count,
               required_capacity) &&
           is_valid_cloth_cloth_candidate_buffer_view(views.collision_candidates) &&
           views.collision_candidates.vertex_capacity >= views.cloth_motion.vertex_count &&
           views.collision_candidates.cloth_cloth_vertex_face.capacity >= required_capacity;
}

void ClothClothCollisionDetector::detect(const SimulationGpuView& views, QOpenGLFunctions_4_5_Core& gl) const
{
    detect(views, detection_distance_, gl);
}

void ClothClothCollisionDetector::detect_initial(const SimulationGpuView& views,
                                                 QOpenGLFunctions_4_5_Core& gl) const
{
    detect(views, initial_detection_distance_, gl);
}

void ClothClothCollisionDetector::detect(const SimulationGpuView& views,
                                         float bounds_margin,
                                         QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_detect(views));

    const CollisionCandidateBuffer& collision_candidates = views.collision_candidates.cloth_cloth_vertex_face;
    if (active_garment_count(views.garment_buffer_states) < 2u) {
        if (is_valid_collision_candidate_buffer(collision_candidates)) {
            views.collision_candidates.clear_cloth_cloth_candidate_counts(gl);
            build_dispatch_size(collision_candidates, gl);
        }
        return;
    }

    bounds_updater_.update(views, bounds_margin, gl);
    views.collision_candidates.clear_cloth_cloth_candidate_counts(gl);

    gl.glUseProgram(candidate_detect_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        candidate_detect_binding::cloth_current,
                        views.cloth_motion.current_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        candidate_detect_binding::cloth_previous,
                        views.cloth_motion.previous_position_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        candidate_detect_binding::triangle_bounds,
                        views.cloth_bvh.bounds_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        candidate_detect_binding::bvh_nodes,
                        views.cloth_bvh.node_buffer);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        candidate_detect_binding::candidates,
                        collision_candidates.candidates);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        candidate_detect_binding::candidate_count,
                        collision_candidates.candidate_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        candidate_detect_binding::overflow_count,
                        collision_candidates.overflow_count);
    gl.glProgramUniform1ui(candidate_detect_.program,
                           candidate_detect_.max_candidates,
                           collision_candidates.capacity);

    detect_pair(views, gl);

    build_dispatch_size(collision_candidates, gl);
}

void ClothClothCollisionDetector::release(QOpenGLFunctions_4_5_Core& gl)
{
    bounds_updater_.release(gl);
    gl.glDeleteProgram(dispatch_size_.program);
    gl.glDeleteProgram(candidate_detect_.program);
    candidate_detect_ = {};
    dispatch_size_ = {};
}

void ClothClothCollisionDetector::detect_pair(const SimulationGpuView& views,
                                              QOpenGLFunctions_4_5_Core& gl) const
{
    const GarmentBufferState& upper_garment_state = views.garment_buffer_states[GarmentLayer::Upper];
    const GarmentBufferState& lower_garment_state = views.garment_buffer_states[GarmentLayer::Lower];
    const CollisionCandidateBuffer& collision_candidates = views.collision_candidates.cloth_cloth_vertex_face;
    gl.glProgramUniform1ui(candidate_detect_.program,
                           candidate_detect_.upper_vertex_offset,
                           upper_garment_state.vertex_start_index);
    gl.glProgramUniform1ui(candidate_detect_.program,
                           candidate_detect_.upper_vertex_count,
                           upper_garment_state.vertex_count);
    gl.glProgramUniform1ui(candidate_detect_.program,
                           candidate_detect_.upper_bvh_root,
                           upper_garment_state.bvh_level_offsets.front());
    gl.glProgramUniform1ui(candidate_detect_.program,
                           candidate_detect_.lower_vertex_offset,
                           lower_garment_state.vertex_start_index);
    gl.glProgramUniform1ui(candidate_detect_.program,
                           candidate_detect_.lower_vertex_count,
                           lower_garment_state.vertex_count);
    gl.glProgramUniform1ui(candidate_detect_.program,
                           candidate_detect_.lower_bvh_root,
                           lower_garment_state.bvh_level_offsets.front());

    const std::uint32_t query_vertex_count =
        upper_garment_state.vertex_count + lower_garment_state.vertex_count;
    gl.glDispatchCompute(compute_group_count(query_vertex_count, candidate_detect_local_size), 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void ClothClothCollisionDetector::build_dispatch_size(const CollisionCandidateBuffer& collision_candidates,
                                                      QOpenGLFunctions_4_5_Core& gl) const
{
    gl.glUseProgram(dispatch_size_.program);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        dispatch_size_binding::candidate_count,
                        collision_candidates.candidate_count);
    gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                        dispatch_size_binding::dispatch_size,
                        collision_candidates.dispatch_size);
    gl.glProgramUniform1ui(dispatch_size_.program,
                           dispatch_size_.max_candidates,
                           collision_candidates.capacity);
    gl.glProgramUniform1ui(dispatch_size_.program,
                           dispatch_size_.local_size,
                           candidate_accumulate_local_size);
    gl.glDispatchCompute(1, 1, 1);
    gl.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
}
