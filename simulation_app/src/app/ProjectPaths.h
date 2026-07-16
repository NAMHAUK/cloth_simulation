#pragma once

#include <filesystem>

struct ShaderPaths {
    std::filesystem::path shader_dir;
    std::filesystem::path viewer_vertex;
    std::filesystem::path viewer_fragment;
    std::filesystem::path background_vertex;
    std::filesystem::path background_fragment;
    std::filesystem::path triangle_normal_compute;
    std::filesystem::path vertex_normal_compute;
    std::filesystem::path character_vertex_position_update_compute;
    std::filesystem::path character_triangle_geometry_update_compute;
    std::filesystem::path character_bvh_bounds_update_compute;
    std::filesystem::path cloth_bvh_bounds_update_compute;
    std::filesystem::path cloth_external_force_compute;
    std::filesystem::path cloth_stretch_constraint_compute;
    std::filesystem::path cloth_bending_constraint_compute;
    std::filesystem::path cloth_attachment_constraint_compute;
    std::filesystem::path cloth_ground_collision_compute;
    std::filesystem::path collision_pair_dispatch_size_compute;
    std::filesystem::path cloth_vertex_body_face_pair_detect_compute;
    std::filesystem::path cloth_vertex_body_face_pair_accumulate_compute;
    std::filesystem::path body_vertex_cloth_face_pair_detect_compute;
    std::filesystem::path body_vertex_cloth_face_pair_accumulate_compute;
    std::filesystem::path cloth_edge_body_edge_pair_detect_compute;
    std::filesystem::path cloth_edge_body_edge_pair_accumulate_compute;
    std::filesystem::path cloth_body_collision_apply_compute;
    std::filesystem::path garment_prefit_compute;
    std::filesystem::path garment_attachment_target_build_compute;
};

struct ProjectPaths {
    std::filesystem::path root;
    ShaderPaths shaders;
    std::filesystem::path python;
    std::filesystem::path converter_script;
    std::filesystem::path smpl_model_path;
    std::filesystem::path motion_asset_dir;
    std::filesystem::path default_character_motion_path;
    std::filesystem::path amass_dir;
    std::filesystem::path garment_source_dir;
    std::filesystem::path garment_asset_dir;
};

inline ShaderPaths make_shader_paths(const std::filesystem::path& project_root)
{
    const std::filesystem::path shader_dir = project_root / "simulation_app" / "shaders";
    return {
        shader_dir,
        shader_dir / "rendering" / "viewer.vert",
        shader_dir / "rendering" / "viewer.frag",
        shader_dir / "rendering" / "background.vert",
        shader_dir / "rendering" / "background.frag",
        shader_dir / "mesh" / "triangle_normal.comp",
        shader_dir / "mesh" / "vertex_normal.comp",
        shader_dir / "character" / "character_vertex_position_update.comp",
        shader_dir / "character" / "character_triangle_geometry_update.comp",
        shader_dir / "character" / "character_bvh_bounds_update.comp",
        shader_dir / "cloth" / "cloth_bvh_bounds_update.comp",
        shader_dir / "cloth" / "cloth_external_force.comp",
        shader_dir / "cloth" / "constraints" / "cloth_stretch_constraint.comp",
        shader_dir / "cloth" / "constraints" / "cloth_bending_constraint.comp",
        shader_dir / "cloth" / "constraints" / "cloth_attachment_constraint.comp",
        shader_dir / "collision" / "cloth_ground_collision.comp",
        shader_dir / "collision" / "collision_pair_dispatch_size.comp",
        shader_dir / "collision" / "cloth_vertex_body_face_pair_detect.comp",
        shader_dir / "collision" / "cloth_vertex_body_face_pair_accumulate.comp",
        shader_dir / "collision" / "body_vertex_cloth_face_pair_detect.comp",
        shader_dir / "collision" / "body_vertex_cloth_face_pair_accumulate.comp",
        shader_dir / "collision" / "cloth_edge_body_edge_pair_detect.comp",
        shader_dir / "collision" / "cloth_edge_body_edge_pair_accumulate.comp",
        shader_dir / "collision" / "cloth_body_collision_apply.comp",
        shader_dir / "cloth" / "setup" / "garment_prefit.comp",
        shader_dir / "cloth" / "setup" / "garment_attachment_target_build.comp",
    };
}

inline ProjectPaths make_project_paths(const std::filesystem::path& project_root)
{
    return {
        project_root,
        make_shader_paths(project_root),
        project_root / "envs" / "cloth-sim" / "python.exe",
        project_root / "simulation_app" / "tools" / "motion_converter" / "convert_amass_motion.py",
        project_root / "data" / "sources" / "smpl" / "models" / "basicmodel_neutral_lbs_10_207_0_v1.1.0.pkl",
        project_root / "data" / "runtime_assets" / "motions",
        project_root / "data" / "runtime_assets" / "motions" / "init" / "a_pose.motion",
        project_root / "data" / "sources" / "amass",
        project_root / "data" / "sources" / "garments",
        project_root / "data" / "runtime_assets" / "garments",
    };
}
