#pragma once

#include "asset/AssetDataTypes.h"

#include <filesystem>

enum class AttachmentType
{
    None,
    Waistband,
};

bool read_garment_obj(const std::filesystem::path& obj_path,
                      AttachmentType attachment_type,
                      GarmentMesh& garment_mesh);
bool write_garment_asset(const std::filesystem::path& garment_asset_path, const GarmentMesh& garment_mesh);
