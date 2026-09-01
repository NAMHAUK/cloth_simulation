#pragma once

#include "app/ProjectPaths.h"
#include "asset/AssetDataTypes.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace asset_io {
// Motion //
bool read_character_motion(const std::filesystem::path& motion_asset_path, CharacterMotion& character_motion);
void read_default_character(const std::filesystem::path& motion_asset_path,
                            CharacterMotion& character_motion,
                            std::vector<std::uint8_t>& triangle_part_labels);

// Garment //
GarmentMesh read_garment_mesh(const std::filesystem::path& garment_asset_path);
void write_garment_asset(const std::filesystem::path& garment_asset_path, const GarmentMesh& garment_mesh);
std::filesystem::path make_garment_asset_path(const ProjectPaths& project_paths,
                                              const std::filesystem::path& garment_obj_path);

// Common //
std::vector<std::filesystem::path> scan_asset_paths(const std::filesystem::path& asset_dir,
                                                    const std::filesystem::path& asset_extension);
}
