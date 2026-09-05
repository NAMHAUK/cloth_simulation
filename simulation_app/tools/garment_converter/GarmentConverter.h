#pragma once

#include "asset/AssetDataTypes.h"

#include <filesystem>

void convert_garment(const std::filesystem::path& obj_path,
                     const std::filesystem::path& garment_asset_path,
                     GarmentCategory garment_category);
