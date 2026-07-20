#include "asset/AssetConverterCommands.h"

#include "simulation/SimulationSettings.h"

#include <filesystem>
#include <iostream>

#include <QCoreApplication>
#include <QString>
#include <QStringList>

namespace {
QString to_q_string(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

bool prepare_converter_paths(
    const ProjectPaths& project_paths,
    const std::filesystem::path& motion_asset_path,
    ConverterCommand& command)
{
    if (!std::filesystem::exists(project_paths.python)) {
        command.error_message = "Missing project Python: " + project_paths.python.string();
        std::cerr << command.error_message << '\n';
        return false;
    }
    if (!std::filesystem::exists(project_paths.converter_script)) {
        command.error_message = "Missing converter script: " + project_paths.converter_script.string();
        std::cerr << command.error_message << '\n';
        return false;
    }

    std::filesystem::create_directories(motion_asset_path.parent_path());
    return true;
}

std::filesystem::path make_garment_converter_exe_path()
{
    std::filesystem::path application_dir = QCoreApplication::applicationDirPath().toStdWString();
    return application_dir / "garment_converter.exe";
}
}

namespace asset_converter_commands {
ConverterCommand make_motion_command(
    const ProjectPaths& project_paths,
    const std::filesystem::path& amass_motion_path,
    const std::filesystem::path& motion_asset_path)
{
    ConverterCommand command;

    if (!prepare_converter_paths(project_paths, motion_asset_path, command)) {
        return command;
    }

    command.program = to_q_string(project_paths.python);
    command.arguments = {
        to_q_string(project_paths.converter_script),
        "--input", to_q_string(amass_motion_path),
        "--neutral-model", to_q_string(project_paths.neutral_smpl_model_path),
        "--male-model", to_q_string(project_paths.male_smpl_model_path),
        "--female-model", to_q_string(project_paths.female_smpl_model_path),
        "--output", to_q_string(motion_asset_path),
        "--target-fps", QString::number(simulation_settings::character_motion_fps),
        "--batch-size", "128",
    };
    command.working_directory = to_q_string(project_paths.root);

    std::cout << "Prepared converter command...\n"
              << "  program=" << project_paths.python << '\n'
              << "  amass_motion=" << amass_motion_path << '\n'
              << "  motion_asset=" << motion_asset_path << '\n';

    command.is_valid = true;
    return command;
}

ConverterCommand make_garment_command(
    const ProjectPaths& project_paths,
    const std::filesystem::path& garment_obj_path,
    const std::filesystem::path& garment_asset_path,
    const QString& attachment_type)
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
        "--attachment-type", attachment_type,
    };
    command.working_directory = to_q_string(project_paths.root);

    std::cout << "Prepared garment converter command...\n"
              << "  program=" << converter_exe_path << '\n'
              << "  garment_obj=" << garment_obj_path << '\n'
              << "  garment_asset=" << garment_asset_path << '\n'
              << "  attachment_type=" << attachment_type.toStdString() << '\n';

    command.is_valid = true;
    return command;
}
}
