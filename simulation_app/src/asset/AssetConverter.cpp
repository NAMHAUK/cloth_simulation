#include "asset/AssetConverter.h"

#include "simulation/SimulationParams.h"
#include "utils/QtUtils.h"

#include <filesystem>
#include <iostream>

#include <QCoreApplication>
#include <QtLogging>

namespace {
std::filesystem::path make_garment_converter_exe_path()
{
    std::filesystem::path application_dir = QCoreApplication::applicationDirPath().toStdWString();
    return application_dir / "garment_converter.exe";
}
}

AssetConverter::AssetConverter(const ProjectPaths& project_paths, QObject* parent)
    : QObject(parent),
      project_paths_(project_paths),
      garment_converter_exe_path_(make_garment_converter_exe_path())
{
    if (!std::filesystem::exists(project_paths_.python)) {
        qFatal("Missing project Python: %s", project_paths_.python.string().c_str());
    }
    if (!std::filesystem::exists(project_paths_.converter_script)) {
        qFatal("Missing converter script: %s", project_paths_.converter_script.string().c_str());
    }
    if (!std::filesystem::exists(garment_converter_exe_path_)) {
        qFatal("Missing garment converter executable: %s", garment_converter_exe_path_.string().c_str());
    }
}

AssetConverter::~AssetConverter()
{
    if (!process_) {
        return;
    }

    disconnect(process_, nullptr, this, nullptr);
    process_->terminate();
    if (!process_->waitForFinished(1000)) {
        process_->kill();
        process_->waitForFinished(1000);
    }
}

// Conversion
void AssetConverter::start_motion_conversion(const std::filesystem::path& amass_motion_path,
                                             const std::filesystem::path& motion_asset_path)
{
    start_conversion(make_motion_command(amass_motion_path, motion_asset_path));
}

void AssetConverter::start_garment_conversion(const std::filesystem::path& garment_obj_path,
                                              const std::filesystem::path& garment_asset_path,
                                              const QString& garment_category)
{
    start_conversion(make_garment_command(garment_obj_path, garment_asset_path, garment_category));
}

void AssetConverter::start_conversion(const ConverterCommand& command)
{
    if (process_) {
        return;
    }

    process_ = new QProcess(this);
    process_->setProgram(command.program);
    process_->setArguments(command.arguments);
    process_->setWorkingDirectory(command.working_directory);
    process_->setProcessChannelMode(QProcess::ForwardedChannels);

    connect_process();
    process_->start();
}

void AssetConverter::connect_process()
{
    connect(
        process_,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this](int exit_code, QProcess::ExitStatus exit_status) { finish_process(exit_code, exit_status); });

    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            finish_process(-1, QProcess::CrashExit);
        }
    });
}

void AssetConverter::finish_process(int exit_code, QProcess::ExitStatus exit_status)
{
    const bool succeeded = exit_status == QProcess::NormalExit && exit_code == 0;

    process_->deleteLater();
    process_ = nullptr;

    if (succeeded) {
        Q_EMIT conversion_succeeded();
    } else {
        Q_EMIT conversion_failed();
    }
}

// Command Preparation
AssetConverter::ConverterCommand AssetConverter::make_motion_command(
    const std::filesystem::path& amass_motion_path,
    const std::filesystem::path& motion_asset_path) const
{
    ConverterCommand command;

    command.program = to_q_string(project_paths_.python);
    command.arguments = {
        to_q_string(project_paths_.converter_script),
        "--input",
        to_q_string(amass_motion_path),
        "--neutral-model",
        to_q_string(project_paths_.neutral_smpl_model_path),
        "--male-model",
        to_q_string(project_paths_.male_smpl_model_path),
        "--female-model",
        to_q_string(project_paths_.female_smpl_model_path),
        "--output",
        to_q_string(motion_asset_path),
        "--target-fps",
        QString::number(default_simulation_params.step.motion_fps),
        "--batch-size",
        "128",
    };
    command.working_directory = to_q_string(project_paths_.root);

    std::cout << "Prepared converter command...\n"
              << "  program=" << project_paths_.python << '\n'
              << "  amass_motion=" << amass_motion_path << '\n'
              << "  motion_asset=" << motion_asset_path << '\n';

    return command;
}

AssetConverter::ConverterCommand AssetConverter::make_garment_command(
    const std::filesystem::path& garment_obj_path,
    const std::filesystem::path& garment_asset_path,
    const QString& garment_category) const
{
    ConverterCommand command;

    command.program = to_q_string(garment_converter_exe_path_);
    command.arguments = {
        "--input",
        to_q_string(garment_obj_path),
        "--output",
        to_q_string(garment_asset_path),
        "--garment-category",
        garment_category,
    };
    command.working_directory = to_q_string(project_paths_.root);

    std::cout << "Prepared garment converter command...\n"
              << "  program=" << garment_converter_exe_path_ << '\n'
              << "  garment_obj=" << garment_obj_path << '\n'
              << "  garment_asset=" << garment_asset_path << '\n'
              << "  garment_category=" << garment_category.toStdString() << '\n';

    return command;
}

// Accessors
bool AssetConverter::is_running() const
{
    return process_ != nullptr;
}
