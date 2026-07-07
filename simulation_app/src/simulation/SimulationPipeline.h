#pragma once

#include "gpu/body/CharacterGpuDataTypes.h"
#include "gpu/cloth/ClothGpuDataTypes.h"
#include "gpu/scene/CollisionContactBuffers.h"
#include "simulation/forces/SimulationForceField.h"
#include "simulation/forces/ExternalForceSolver.h"
#include "simulation/constraints/StretchConstraintSolver.h"
#include "simulation/constraints/BendingConstraintSolver.h"
#include "simulation/constraints/AttachmentConstraintSolver.h"
#include "simulation/collision/GroundCollisionSolver.h"
#include "simulation/collision/BodyVertexClothFaceCollisionSolver.h"
#include "simulation/collision/ClothEdgeBodyEdgeCollisionSolver.h"
#include "simulation/collision/ClothVertexBodyFaceCollisionSolver.h"
#include "simulation/collision/ClothBodyContactGenerator.h"
#include "simulation/collision/GarmentPrefitSolver.h"
#include "utils/GpuElapsedTimer.h"

#include <cstdint>

#include <QOpenGLFunctions_4_5_Core>

// GPU timing stays opt-in at compile time because GL_TIME_ELAPSED has runtime cost.
#ifndef CLOTH_SIM_SUBSTEP_GPU_TIMING
#define CLOTH_SIM_SUBSTEP_GPU_TIMING 1
#endif

class SceneGpuState;
class SceneState;
struct ShaderPaths;

class SimulationPipeline final {
public:
    SimulationPipeline() = default;
    SimulationPipeline(const SimulationPipeline&) = delete;
    SimulationPipeline& operator=(const SimulationPipeline&) = delete;

    bool is_initialized() const;
    bool initialize(const ShaderPaths& shader_paths, QOpenGLFunctions_4_5_Core& gl);
    bool prefit_garments(SceneState& scene, SceneGpuState& gpu_state, QOpenGLFunctions_4_5_Core& gl);
    bool step(SceneState& scene, SceneGpuState& gpu_state, std::uint64_t motion_step_index, QOpenGLFunctions_4_5_Core& gl);
    void release(QOpenGLFunctions_4_5_Core& gl);

private:
    struct SimulationGpuViews final {
        ClothMotionBufferView cloth_motion;
        ClothCollisionStateBufferView cloth_collision;
        ClothMeshTopologyResources cloth_topology;
        CharacterVertexBufferView character_vertices;
        TriangleGeometryResources character_geometry;
        TriangleBvhResources character_bvh;
        VertexBvhResources body_vertex_bvh;
        EdgeBvhResources body_edge_bvh;
        CollisionContactBufferView collision_contacts;
        DistanceConstraintBufferView stretch_constraints;
        DistanceConstraintBufferView bending_constraints;
        AttachmentConstraintBufferView attachment_constraints;
    };

    static SimulationGpuViews collect_gpu_views(const SceneGpuState& gpu_state);
    bool can_solve_constraint_iteration(const SimulationGpuViews& views) const;

    SimulationForceField force_field_;
    ExternalForceSolver external_force_solver_;
    StretchConstraintSolver stretch_constraint_solver_;
    BendingConstraintSolver bending_constraint_solver_;
    AttachmentConstraintSolver attachment_constraint_solver_;
    GroundCollisionSolver ground_collision_solver_;
    ClothBodyContactGenerator cloth_body_contact_generator_;
    BodyVertexClothFaceCollisionSolver body_vertex_cloth_face_collision_solver_;
    ClothEdgeBodyEdgeCollisionSolver cloth_edge_body_edge_collision_solver_;
    ClothVertexBodyFaceCollisionSolver cloth_vertex_body_face_collision_solver_;
    GarmentPrefitSolver garment_prefit_solver_;
#if CLOTH_SIM_SUBSTEP_GPU_TIMING
    mutable GpuElapsedTimer substep_gpu_timer_;
#endif
    float substep_dt_ = 0.0f;
    bool initialized_ = false;
};
