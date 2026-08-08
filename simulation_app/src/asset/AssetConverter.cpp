#include "asset/AssetConverter.h"

#include <iostream>
#include <utility>

AssetConverter::AssetConverter(QObject* parent) : QObject(parent)
{}

AssetConverter::~AssetConverter()
{
    conversion_succeeded_callback_ = {};
    conversion_failed_callback_ = {};

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

bool AssetConverter::is_running() const
{
    return process_ != nullptr;
}

void AssetConverter::set_conversion_succeeded_callback(ConversionSucceededCallback callback)
{
    conversion_succeeded_callback_ = std::move(callback);
}

void AssetConverter::set_conversion_failed_callback(ConversionFailedCallback callback)
{
    conversion_failed_callback_ = std::move(callback);
}

void AssetConverter::start_conversion(const ConverterCommand& command)
{
    if (process_) {
        return;
    }

    if (!command.is_valid) {
        if (conversion_failed_callback_) {
            conversion_failed_callback_(command.error_message);
        }
        return;
    }

    result_ = {};
    process_ = new QProcess(this);
    process_->setProgram(command.program);
    process_->setArguments(command.arguments);
    process_->setWorkingDirectory(command.working_directory);

    setup_process_callbacks();
    process_->start();
}

void AssetConverter::setup_process_callbacks()
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
        if (conversion_succeeded_callback_) {
            conversion_succeeded_callback_();
        }
    } else if (conversion_failed_callback_) {
        conversion_failed_callback_(result_.error_message);
    }

    result_ = {};
}
