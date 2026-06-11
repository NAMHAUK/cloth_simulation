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
    std::filesystem::path character_triangle_geometry_update_compute;
    std::filesystem::path character_bvh_bounds_update_compute;
    std::filesystem::path cloth_external_force_compute;
    std::filesystem::path cloth_stretch_constraint_compute;
    std::filesystem::path cloth_bending_constraint_compute;
    std::filesystem::path cloth_ground_collision_compute;
    std::filesystem::path cloth_character_collision_compute;
};

struct ProjectPaths {
    std::filesystem::path root;
    ShaderPaths shaders;
    std::filesystem::path python;
    std::filesystem::path converter_script;
    std::filesystem::path smpl_model_dir;
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
        shader_dir / "viewer.vert",
        shader_dir / "viewer.frag",
        shader_dir / "background.vert",
        shader_dir / "background.frag",
        shader_dir / "triangle_normal.comp",
        shader_dir / "vertex_normal.comp",
        shader_dir / "character_triangle_geometry_update.comp",
        shader_dir / "character_bvh_bounds_update.comp",
        shader_dir / "cloth_external_force.comp",
        shader_dir / "cloth_stretch_constraint.comp",
        shader_dir / "cloth_bending_constraint.comp",
        shader_dir / "cloth_ground_collision.comp",
        shader_dir / "cloth_character_collision.comp",
    };
}

inline ProjectPaths make_project_paths(const std::filesystem::path& project_root)
{
    return {
        project_root,
        make_shader_paths(project_root),
        project_root / "envs" / "cloth-sim" / "python.exe",
        project_root / "simulation_app" / "tools" / "motion_converter" / "convert_amass_to_cache.py",
        project_root / "data" / "sources" / "smpl" / "models",
        project_root / "data" / "runtime_assets" / "motions",
        project_root / "data" / "runtime_assets" / "motions" / "init" / "t_pose.cache",
        project_root / "data" / "sources" / "amass",
        project_root / "data" / "sources" / "garments",
        project_root / "data" / "runtime_assets" / "garments",
    };
}
