#pragma once

#include "asset/GarmentAsset.h"

#include <filesystem>

bool read_garment_obj(const std::filesystem::path& obj_path, GarmentMesh& garment_mesh);
bool write_garment_asset(const std::filesystem::path& garment_asset_path, const GarmentMesh& garment_mesh);
