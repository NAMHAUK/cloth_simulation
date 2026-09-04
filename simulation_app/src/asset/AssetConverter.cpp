#include "asset/AssetConverter.h"

#include "simulation/SimulationParams.h"
#include "utils/QtUtils.h"

#include <filesystem>
#include <iostream>
#include <utility>

#include <QCoreApplication>

namespace {
std::filesystem::path make_garment_converter_exe_path()
{
    std::filesystem::path application_dir = QCoreApplication::applicationDirPath().toStdWString();
    return application_dir / "garment_converter.exe";
}
}

AssetConverter::AssetConverter(QObject* parent) : QObject(parent)
{}

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
void AssetConverter::start_motion_conversion(const ProjectPaths& project_paths,
                                             const std::filesystem::path& amass_motion_path,
                                             const std::filesystem::path& motion_asset_path)
{
    start_conversion(make_motion_command(project_paths, amass_motion_path, motion_asset_path));
}

void AssetConverter::start_garment_conversion(const ProjectPaths& project_paths,
                                              const std::filesystem::path& garment_obj_path,
                                              const std::filesystem::path& garment_asset_path,
                                              const QString& garment_category)
{
    start_conversion(
        make_garment_command(project_paths, garment_obj_path, garment_asset_path, garment_category));
}

void AssetConverter::start_conversion(const ConverterCommand& command)
{
    if (process_) {
        return;
    }

    if (!command.is_valid) {
        Q_EMIT conversion_failed(command.error_message);
        return;
    }

    result_ = {};
    process_ = new QProcess(this);
    process_->setProgram(command.program);
    process_->setArguments(command.arguments);
    process_->setWorkingDirectory(command.working_directory);

    connect_process();
    process_->start();
}

void AssetConverter::connect_process()
{
    connect(process_, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!process_) {
            return;
        }

        std::cout << process_->readAllStandardOutput().toStdString();
    });

    connect(process_, &QProcess::readyReadStandardError, this, [this]() {
        if (!process_) {
            return;
        }

        std::cerr << process_->readAllStandardError().toStdString();
    });

    connect(
        process_,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this](int exit_code, QProcess::ExitStatus exit_status) { finish_process(exit_code, exit_status); });

    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!process_) {
            return;
        }

        result_.error_message = process_->errorString().toStdString();
        if (error == QProcess::FailedToStart) {
            finish_process(-1, QProcess::CrashExit);
        }
    });
}

void AssetConverter::finish_process(int exit_code, QProcess::ExitStatus exit_status)
{
    if (!process_) {
        return;
    }

    result_.exit_code = exit_code;

    std::cout << process_->readAllStandardOutput().toStdString();
    std::cerr << process_->readAllStandardError().toStdString();

    result_.succeeded = exit_status == QProcess::NormalExit && exit_code == 0;
    if (!result_.succeeded && result_.error_message.empty()) {
        result_.error_message = exit_status == QProcess::NormalExit
                                    ? "Converter failed with exit code: " + std::to_string(exit_code)
                                    : "Converter process crashed.";
    }

    QProcess* finished_process = process_;
    process_ = nullptr;
    finished_process->deleteLater();

    if (result_.succeeded) {
        Q_EMIT conversion_succeeded();
    } else {
        Q_EMIT conversion_failed(result_.error_message);
    }

    result_ = {};
}

// Command Preparation
AssetConverter::ConverterCommand AssetConverter::make_motion_command(
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
        "--input",
        to_q_string(amass_motion_path),
        "--neutral-model",
        to_q_string(project_paths.neutral_smpl_model_path),
        "--male-model",
        to_q_string(project_paths.male_smpl_model_path),
        "--female-model",
        to_q_string(project_paths.female_smpl_model_path),
        "--output",
        to_q_string(motion_asset_path),
        "--target-fps",
        QString::number(default_simulation_params.step.motion_fps),
        "--batch-size",
        "128",
    };
    command.working_directory = to_q_string(project_paths.root);

    std::cout << "Prepared converter command...\n"
              << "  program=" << project_paths.python << '\n'
              << "  amass_motion=" << amass_motion_path << '\n'
              << "  motion_asset=" << motion_asset_path << '\n';

    command.is_valid = true;
    return command;
}

AssetConverter::ConverterCommand AssetConverter::make_garment_command(
    const ProjectPaths& project_paths,
    const std::filesystem::path& garment_obj_path,
    const std::filesystem::path& garment_asset_path,
    const QString& garment_category)
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
        command.error_message =
            "Failed to create garment asset directory: " + garment_asset_path.parent_path().string();
        std::cerr << command.error_message << '\n';
        return command;
    }

    command.program = to_q_string(converter_exe_path);
    command.arguments = {
        "--input",
        to_q_string(garment_obj_path),
        "--output",
        to_q_string(garment_asset_path),
        "--garment-category",
        garment_category,
    };
    command.working_directory = to_q_string(project_paths.root);

    std::cout << "Prepared garment converter command...\n"
              << "  program=" << converter_exe_path << '\n'
              << "  garment_obj=" << garment_obj_path << '\n'
              << "  garment_asset=" << garment_asset_path << '\n'
              << "  garment_category=" << garment_category.toStdString() << '\n';

    command.is_valid = true;
    return command;
}

bool AssetConverter::prepare_converter_paths(const ProjectPaths& project_paths,
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

// Accessors
bool AssetConverter::is_running() const
{
    return process_ != nullptr;
}
