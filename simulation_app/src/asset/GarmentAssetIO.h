#pragma once

#include "asset/GarmentAsset.h"

#include <cstdint>
#include <filesystem>

namespace garment_asset_io {
inline constexpr std::uint32_t position_components = 3;

bool is_valid_garment_mesh(const GarmentMesh& garment_mesh);
bool read_garment_asset_file(const std::filesystem::path& garment_asset_path, GarmentMesh& garment_mesh);
bool write_garment_asset_file(const std::filesystem::path& garment_asset_path, const GarmentMesh& garment_mesh);
}
