#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <QString>
#include <QStringList>

#include <glm/vec3.hpp>

#include "app/ProjectPaths.h"

struct CharacterMesh {
    float fps = 0.0f;
    std::uint32_t frame_count = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
    std::vector<std::uint32_t> indices;
    std::vector<float> vertices;
    glm::vec3 bounds_center{};
    float bounds_radius = 1.0f;
};

struct MotionAsset {
    std::filesystem::path motion_asset_path;
    std::string display_name;
};

struct ConverterResult {
    bool succeeded = false;
    int exit_code = -1;
    std::string error_message;
};

struct ConverterCommand {
    bool is_valid = false;
    QString program;
    QStringList arguments;
    QString working_directory;
    std::string error_message;
};

bool read_character_mesh_asset(const std::filesystem::path& motion_asset_path, CharacterMesh& character_mesh);
ConverterCommand make_converter_command(
    const ProjectPaths& project_paths,
    const std::filesystem::path& amass_motion_path,
    const std::filesystem::path& motion_asset_path);

std::vector<MotionAsset> scan_motion_assets(const ProjectPaths& project_paths);
std::filesystem::path make_motion_asset_path(const ProjectPaths& project_paths, const std::filesystem::path& amass_motion_path);
