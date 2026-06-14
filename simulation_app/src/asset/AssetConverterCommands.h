#pragma once

#include "app/ProjectPaths.h"
#include "asset/AssetConverter.h"

#include <filesystem>

#include <QString>

namespace asset_converter_commands {
ConverterCommand make_motion_command(const ProjectPaths& project_paths,
                                     const std::filesystem::path& amass_motion_path,
                                     const std::filesystem::path& motion_asset_path);
ConverterCommand make_garment_command(const ProjectPaths& project_paths,
                                      const std::filesystem::path& garment_obj_path,
                                      const std::filesystem::path& garment_asset_path,
                                      const QString& attachment_type);
}
