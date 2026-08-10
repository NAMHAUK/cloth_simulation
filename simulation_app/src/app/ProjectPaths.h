#pragma once

#include <filesystem>

struct ProjectPaths
{
    std::filesystem::path root;
    std::filesystem::path shader_dir;
    std::filesystem::path python;
    std::filesystem::path converter_script;
    std::filesystem::path neutral_smpl_model_path;
    std::filesystem::path male_smpl_model_path;
    std::filesystem::path female_smpl_model_path;
    std::filesystem::path motion_asset_dir;
    std::filesystem::path default_character_path;
    std::filesystem::path amass_dir;
    std::filesystem::path garment_source_dir;
    std::filesystem::path garment_asset_dir;
};

inline ProjectPaths make_project_paths(const std::filesystem::path& project_root)
{
    return {
        project_root,
        project_root / "simulation_app" / "shaders",
        project_root / "envs" / "cloth-sim" / "python.exe",
        project_root / "simulation_app" / "tools" / "motion_converter" / "convert_amass_motion.py",
        project_root / "data" / "sources" / "smpl" / "models" / "basicmodel_neutral_lbs_10_207_0_v1.1.0.pkl",
        project_root / "data" / "sources" / "smpl" / "models" / "basicmodel_m_lbs_10_207_0_v1.1.0.pkl",
        project_root / "data" / "sources" / "smpl" / "models" / "basicmodel_f_lbs_10_207_0_v1.1.0.pkl",
        project_root / "data" / "runtime_assets" / "motions",
        project_root / "data" / "runtime_assets" / "motions" / "init" / "a_pose.motion",
        project_root / "data" / "sources" / "amass",
        project_root / "data" / "sources" / "garments",
        project_root / "data" / "runtime_assets" / "garments",
    };
}
