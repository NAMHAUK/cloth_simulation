#pragma once

#include "app/ProjectPaths.h"
#include "asset/AssetDataTypes.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace asset_io {
inline constexpr std::uint32_t position_components = 3;

// Common //
bool is_valid_garment_mesh(const GarmentMesh& garment_mesh);

// Motion //
bool read_character_mesh_asset(const std::filesystem::path& motion_asset_path, CharacterMesh& character_mesh);
bool read_default_character_mesh_asset(const std::filesystem::path& motion_asset_path,
                                       CharacterMesh& character_mesh,
                                       std::vector<std::uint8_t>& triangle_part_labels);
std::vector<std::filesystem::path> scan_motion_asset_paths(const ProjectPaths& project_paths);
std::filesystem::path make_motion_asset_path(const ProjectPaths& project_paths,
                                             const std::filesystem::path& amass_motion_path);

// Garment //
bool read_garment_mesh(const std::filesystem::path& garment_asset_path, GarmentMesh& garment_mesh);
bool write_garment_asset(const std::filesystem::path& garment_asset_path, const GarmentMesh& garment_mesh);
std::vector<std::filesystem::path> scan_garment_asset_paths(const ProjectPaths& project_paths);
std::filesystem::path make_garment_asset_path(const ProjectPaths& project_paths,
                                              const std::filesystem::path& garment_obj_path);
}
