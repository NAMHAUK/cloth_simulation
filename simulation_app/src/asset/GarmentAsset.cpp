#include "asset/GarmentAsset.h"

#include "asset/AssetConverter.h"
#include "asset/GarmentAssetIO.h"
#include "utils/QtUtils.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <utility>

#include <QCoreApplication>

namespace {
std::filesystem::path make_garment_converter_exe_path()
{
    std::filesystem::path application_dir = QCoreApplication::applicationDirPath().toStdWString();
    return application_dir / "garment_converter.exe";
}
}

bool read_garment_asset(const std::filesystem::path& garment_asset_path, GarmentMesh& garment_mesh)
{
    GarmentMesh asset_mesh;
    if (!garment_asset_io::read_garment_asset_file(garment_asset_path, asset_mesh)) {
        return false;
    }

    std::cout << "Loaded garment asset: " << garment_asset_path << '\n';
    std::cout << "  vertices=" << asset_mesh.vertices.size() / garment_asset_io::position_components
              << " triangles=" << asset_mesh.indices.size() / 3u
              << " stretch_constraints=" << asset_mesh.stretch_constraints.colorized_edges.size()
              << " stretch_color_groups=" << asset_mesh.stretch_constraints.color_ranges.size()
              << " bending_constraints=" << asset_mesh.bending_constraints.colorized_edges.size()
              << " bending_color_groups=" << asset_mesh.bending_constraints.color_ranges.size()
              << " bounds_radius=" << asset_mesh.bounds_radius << '\n';

    garment_mesh = std::move(asset_mesh);
    return true;
}

ConverterCommand make_garment_converter_command(
    const ProjectPaths& project_paths,
    const std::filesystem::path& garment_obj_path,
    const std::filesystem::path& garment_asset_path)
{
    ConverterCommand command;

    const std::filesystem::path converter_exe_path = make_garment_converter_exe_path();
    if (!std::filesystem::exists(converter_exe_path)) {
        command.error_message = "Missing garment converter executable: " + converter_exe_path.string();
        std::cerr << command.error_message << '\n';
        return command;
    }

    std::error_code error;
    std::filesystem::create_directories(garment_asset_path.parent_path(), error);
    if (error) {
        command.error_message = "Failed to create garment asset directory: " + garment_asset_path.parent_path().string();
        std::cerr << command.error_message << '\n';
        return command;
    }

    command.program = to_q_string(converter_exe_path);
    command.arguments = {
        "--input", to_q_string(garment_obj_path),
        "--output", to_q_string(garment_asset_path),
    };
    command.working_directory = to_q_string(project_paths.root);

    std::cout << "Prepared garment converter command...\n"
              << "  program=" << converter_exe_path << '\n'
              << "  garment_obj=" << garment_obj_path << '\n'
              << "  garment_asset=" << garment_asset_path << '\n';

    command.is_valid = true;
    return command;
}

std::vector<GarmentAsset> scan_garment_assets(const ProjectPaths& project_paths)
{
    std::vector<GarmentAsset> assets;
    if (!std::filesystem::exists(project_paths.garment_asset_dir)) {
        return assets;
    }

    for (const auto& file : std::filesystem::recursive_directory_iterator(project_paths.garment_asset_dir)) {
        const auto garment_asset_path = file.path();

        if (!file.is_regular_file() || garment_asset_path.extension() != ".garment") {
            continue;
        }

        assets.push_back({
            garment_asset_path,
            garment_asset_path.stem().string(),
        });
    }

    std::sort(assets.begin(), assets.end(), [](const GarmentAsset& lhs, const GarmentAsset& rhs) {
        return lhs.display_name < rhs.display_name;
    });
    return assets;
}
