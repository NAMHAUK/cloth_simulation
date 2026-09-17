#pragma once

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuState;
struct ClothCollisionParams;
struct GarmentBufferState;

class CollisionDetector final
{
public:
    explicit CollisionDetector(const ClothCollisionParams& params);
    CollisionDetector(const CollisionDetector&) = delete;
    CollisionDetector& operator=(const CollisionDetector&) = delete;

    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);

    void detect(const SceneGpuState& gpu_state,
                QOpenGLFunctions_4_5_Core& gl,
                const GLuint* edge_timestamps = nullptr) const;
    void detect_prefit(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;

    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    enum class CandidateKind : std::uint32_t
    {
        ClothVertexBodyFace = 0,
        ClothEdgeBodyEdge = 1,
        ClothFaceBodyVertex = 2,
        ClothClothVertexFace = 3,
        ClothClothEdgeEdge = 4,
    };

    struct CandidateDetectionProgram final
    {
        GLuint program = 0;
        GLint item_count = -1;
        GLint max_candidates = -1;
    };

    struct DispatchSizeProgram final
    {
        GLuint program = 0;
        GLint candidate_kind = -1;
        GLint max_candidates = -1;
    };

    struct ClothClothDetectionProgram final
    {
        GLuint program = 0;
        GLint vertex_offsets = -1;
        GLint vertex_counts = -1;
        GLint bvh_roots = -1;
        GLint triangle_offsets = -1;
        GLint masks_per_vertex = -1;
        GLint exclusion_offsets = -1;
        GLint max_candidates = -1;
    };

    struct EdgeDetectionProgram final
    {
        GLuint program = 0;
        GLint edge_offset = -1;
        GLint edge_count = -1;
        GLint edge_exclusion_offset_loc = -1;
        GLint target_bvh_root = -1;
        GLint is_self_collision = -1;
        GLint max_candidates = -1;
    };

    void detect_cloth_vertex_body_face(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;
    void detect_cloth_edge_body_edge(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;
    void detect_cloth_face_body_vertex(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;
    void detect_cloth_cloth_vertex_face(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;
    void detect_cloth_cloth_edge_edge(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;
    void detect_edges(const GarmentBufferState& source,
                      const GarmentBufferState& target,
                      bool is_self_collision,
                      QOpenGLFunctions_4_5_Core& gl) const;
    void build_dispatch_size(CandidateKind candidate_kind,
                             std::uint32_t max_candidates,
                             QOpenGLFunctions_4_5_Core& gl) const;

    CandidateDetectionProgram cloth_vertex_body_face_;
    CandidateDetectionProgram cloth_edge_body_edge_;
    CandidateDetectionProgram cloth_face_body_vertex_;
    ClothClothDetectionProgram cloth_cloth_vertex_face_;
    EdgeDetectionProgram cloth_cloth_edge_edge_;
    DispatchSizeProgram dispatch_size_;
    float cloth_detection_distance_ = 0.0f;
};
