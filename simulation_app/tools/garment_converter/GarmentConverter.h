#pragma once

#include "asset/AssetDataTypes.h"

#include <filesystem>

GarmentMesh read_garment_obj(const std::filesystem::path& obj_path, GarmentCategory garment_category);
void write_garment_asset(const std::filesystem::path& garment_asset_path, const GarmentMesh& garment_mesh);
