#pragma once

#include "app/ProjectPaths.h"

#include <filesystem>

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class AssetConverter final : public QObject
{
    Q_OBJECT

public:
    explicit AssetConverter(const ProjectPaths& project_paths, QObject* parent = nullptr);
    ~AssetConverter() override;

    void start_motion_conversion(const std::filesystem::path& amass_motion_path,
                                 const std::filesystem::path& motion_asset_path);
    void start_garment_conversion(const std::filesystem::path& garment_obj_path,
                                  const std::filesystem::path& garment_asset_path,
                                  const QString& garment_category);

    bool is_running() const;

Q_SIGNALS:
    void conversion_succeeded();
    void conversion_failed();

private:
    struct ConverterCommand
    {
        QString program;
        QStringList arguments;
        QString working_directory;
    };

    void start_conversion(const ConverterCommand& command);
    void connect_process();
    void finish_process(int exit_code, QProcess::ExitStatus exit_status);

    ConverterCommand make_motion_command(const std::filesystem::path& amass_motion_path,
                                         const std::filesystem::path& motion_asset_path) const;
    ConverterCommand make_garment_command(const std::filesystem::path& garment_obj_path,
                                          const std::filesystem::path& garment_asset_path,
                                          const QString& garment_category) const;

    ProjectPaths project_paths_;
    std::filesystem::path garment_converter_exe_path_;
    QProcess* process_ = nullptr;
};
