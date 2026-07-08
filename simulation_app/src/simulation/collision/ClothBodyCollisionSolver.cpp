#include "simulation/collision/ClothBodyCollisionSolver.h"

#include <cassert>

bool ClothBodyCollisionSolver::is_initialized() const
{
    return cloth_vertex_body_face_.is_initialized() &&
           cloth_edge_body_edge_.is_initialized() &&
           body_vertex_cloth_face_.is_initialized();
}

bool ClothBodyCollisionSolver::initialize(const std::filesystem::path& cloth_vertex_body_face_accumulate_shader_path,
                                          const std::filesystem::path& cloth_vertex_body_face_apply_shader_path,
                                          const std::filesystem::path& cloth_edge_body_edge_accumulate_shader_path,
                                          const std::filesystem::path& cloth_edge_body_edge_apply_shader_path,
                                          const std::filesystem::path& body_vertex_cloth_face_accumulate_shader_path,
                                          const std::filesystem::path& body_vertex_cloth_face_apply_shader_path,
                                          float collision_thickness,
                                          float max_correction_length,
                                          QOpenGLFunctions_4_5_Core& gl)
{
    const bool initialized =
        cloth_vertex_body_face_.initialize(cloth_vertex_body_face_accumulate_shader_path,
                                           cloth_vertex_body_face_apply_shader_path,
                                           collision_thickness,
                                           max_correction_length,
                                           gl) &&
        cloth_edge_body_edge_.initialize(cloth_edge_body_edge_accumulate_shader_path,
                                         cloth_edge_body_edge_apply_shader_path,
                                         collision_thickness,
                                         max_correction_length,
                                         gl) &&
        body_vertex_cloth_face_.initialize(body_vertex_cloth_face_accumulate_shader_path,
                                           body_vertex_cloth_face_apply_shader_path,
                                           collision_thickness,
                                           max_correction_length,
                                           gl);

    if (!initialized) {
        release(gl);
        return false;
    }

    return true;
}

bool ClothBodyCollisionSolver::can_solve(const SimulationGpuViews& views) const
{
    return cloth_vertex_body_face_.can_solve(views.cloth_motion,
                                            views.cloth_collision,
                                            views.character_geometry,
                                            views.collision_contacts) &&
           cloth_edge_body_edge_.can_solve(views.cloth_motion,
                                           views.cloth_collision,
                                           views.stretch_constraints,
                                           views.character_vertices,
                                           views.body_edge_bvh,
                                           views.collision_contacts) &&
           body_vertex_cloth_face_.can_solve(views.cloth_motion,
                                             views.cloth_collision,
                                             views.cloth_topology,
                                             views.character_vertices,
                                             views.collision_contacts);
}

void ClothBodyCollisionSolver::solve(const SimulationGpuViews& views, QOpenGLFunctions_4_5_Core& gl) const
{
    assert(can_solve(views));

    cloth_vertex_body_face_.solve(views.cloth_motion,
                                  views.cloth_collision,
                                  views.character_geometry,
                                  views.collision_contacts,
                                  gl);
    cloth_edge_body_edge_.solve(views.cloth_motion,
                                views.cloth_collision,
                                views.stretch_constraints,
                                views.character_vertices,
                                views.body_edge_bvh,
                                views.collision_contacts,
                                gl);
    body_vertex_cloth_face_.solve(views.cloth_motion,
                                  views.cloth_collision,
                                  views.cloth_topology,
                                  views.character_vertices,
                                  views.collision_contacts,
                                  gl);
}

void ClothBodyCollisionSolver::release(QOpenGLFunctions_4_5_Core& gl)
{
    body_vertex_cloth_face_.release(gl);
    cloth_edge_body_edge_.release(gl);
    cloth_vertex_body_face_.release(gl);
}
