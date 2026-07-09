#pragma once

#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"
#include "gpu/scene/CollisionPairBuffers.h"
#include "simulation/SimulationGpuViews.h"
#include "simulation/collision/BodyVertexClothFaceCollisionSolver.h"
#include "simulation/collision/ClothEdgeBodyEdgeCollisionSolver.h"
#include "simulation/collision/ClothVertexBodyFaceCollisionSolver.h"

#include <filesystem>

#include <QOpenGLFunctions_4_5_Core>

class ClothBodyCollisionSolver final {
public:
    ClothBodyCollisionSolver() = default;
    ClothBodyCollisionSolver(const ClothBodyCollisionSolver&) = delete;
    ClothBodyCollisionSolver& operator=(const ClothBodyCollisionSolver&) = delete;

    bool is_initialized() const;
    bool initialize(const std::filesystem::path& cloth_vertex_body_face_accumulate_shader_path,
                    const std::filesystem::path& cloth_vertex_body_face_apply_shader_path,
                    const std::filesystem::path& cloth_edge_body_edge_accumulate_shader_path,
                    const std::filesystem::path& cloth_edge_body_edge_apply_shader_path,
                    const std::filesystem::path& body_vertex_cloth_face_accumulate_shader_path,
                    const std::filesystem::path& body_vertex_cloth_face_apply_shader_path,
                    float collision_thickness,
                    float max_correction_length,
                    QOpenGLFunctions_4_5_Core& gl);
    bool can_solve(const SimulationGpuViews& views) const;
    void solve(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const;
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    ClothVertexBodyFaceCollisionSolver cloth_vertex_body_face_;
    ClothEdgeBodyEdgeCollisionSolver cloth_edge_body_edge_;
    BodyVertexClothFaceCollisionSolver body_vertex_cloth_face_;
};
