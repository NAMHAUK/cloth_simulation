#pragma once

#include <filesystem>

struct ProjectPaths {
    std::filesystem::path root;
    std::filesystem::path python;
    std::filesystem::path converter_script;
    std::filesystem::path smpl_model_dir;
    std::filesystem::path motion_asset_dir;
    std::filesystem::path amass_dir;
    std::filesystem::path garment_asset_dir;
};

inline ProjectPaths make_project_paths(const std::filesystem::path& project_root)
{
    return {
        project_root,
        project_root / "envs" / "cloth-sim" / "python.exe",
        project_root / "simulation_app" / "tools" / "convert_amass_to_cache.py",
        project_root / "data" / "smpl" / "models",
        project_root / "data" / "cache",
        project_root / "data" / "amass",
        project_root / "data" / "garments",
    };
}
