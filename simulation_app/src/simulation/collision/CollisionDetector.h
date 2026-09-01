#pragma once

#include <cstdint>
#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class SceneGpuState;

class CollisionDetector final
{
public:
    CollisionDetector() = default;
    CollisionDetector(const CollisionDetector&) = delete;
    CollisionDetector& operator=(const CollisionDetector&) = delete;

    void initialize(const std::filesystem::path& shader_dir, QOpenGLFunctions_4_5_Core& gl);

    void detect(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;
    void detect_prefit(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;

    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    enum class CandidateKind : std::uint32_t
    {
        ClothVertexBodyFace = 0,
        ClothEdgeBodyEdge = 1,
        ClothFaceBodyVertex = 2,
        ClothClothVertexFace = 3,
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
        GLint upper_vertex_offset = -1;
        GLint upper_vertex_count = -1;
        GLint upper_bvh_root = -1;
        GLint lower_vertex_offset = -1;
        GLint lower_vertex_count = -1;
        GLint lower_bvh_root = -1;
        GLint max_candidates = -1;
    };

    void detect_cloth_vertex_body_face(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;
    void detect_cloth_edge_body_edge(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;
    void detect_cloth_face_body_vertex(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;
    void detect_cloth_cloth_vertex_face(const SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl) const;
    void build_dispatch_size(CandidateKind candidate_kind,
                             std::uint32_t max_candidates,
                             QOpenGLFunctions_4_5_Core& gl) const;

    CandidateDetectionProgram cloth_vertex_body_face_;
    CandidateDetectionProgram cloth_edge_body_edge_;
    CandidateDetectionProgram cloth_face_body_vertex_;
    ClothClothDetectionProgram cloth_cloth_vertex_face_;
    DispatchSizeProgram dispatch_size_;
};
