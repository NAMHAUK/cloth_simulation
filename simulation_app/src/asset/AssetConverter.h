#pragma once

#include "app/ProjectPaths.h"

#include <filesystem>
#include <string>

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class AssetConverter final : public QObject
{
    Q_OBJECT

public:
    explicit AssetConverter(QObject* parent = nullptr);
    ~AssetConverter() override;

    AssetConverter(const AssetConverter&) = delete;
    AssetConverter& operator=(const AssetConverter&) = delete;

    void start_motion_conversion(const ProjectPaths& project_paths,
                                 const std::filesystem::path& amass_motion_path,
                                 const std::filesystem::path& motion_asset_path);
    void start_garment_conversion(const ProjectPaths& project_paths,
                                  const std::filesystem::path& garment_obj_path,
                                  const std::filesystem::path& garment_asset_path,
                                  const QString& garment_category);

    bool is_running() const;

Q_SIGNALS:
    void conversion_succeeded();
    void conversion_failed(const std::string& error_message);

private:
    struct ConverterResult
    {
        bool succeeded = false;
        int exit_code = -1;
        std::string error_message;
    };

    struct ConverterCommand
    {
        bool is_valid = false;
        QString program;
        QStringList arguments;
        QString working_directory;
        std::string error_message;
    };

    void start_conversion(const ConverterCommand& command);
    void connect_process();
    void finish_process(int exit_code, QProcess::ExitStatus exit_status);

    static ConverterCommand make_motion_command(const ProjectPaths& project_paths,
                                                const std::filesystem::path& amass_motion_path,
                                                const std::filesystem::path& motion_asset_path);
    static ConverterCommand make_garment_command(const ProjectPaths& project_paths,
                                                 const std::filesystem::path& garment_obj_path,
                                                 const std::filesystem::path& garment_asset_path,
                                                 const QString& garment_category);
    static bool prepare_converter_paths(const ProjectPaths& project_paths,
                                        const std::filesystem::path& motion_asset_path,
                                        ConverterCommand& command);

    QProcess* process_ = nullptr;
    ConverterResult result_;
};
