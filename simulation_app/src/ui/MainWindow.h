#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include <QMainWindow>
#include <QProcess>

#include "app/ProjectPaths.h"
#include "assets/MotionAsset.h"

class MotionBrowserPanel;
class QObject;
class OpenGLViewerWidget;
class QEvent;
class QResizeEvent;
class QWidget;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(const std::filesystem::path& project_root, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void update_motion_browser();
    void refresh_motion_list();
    void load_motion_asset(const std::filesystem::path& motion_asset_path);
    void request_garment_asset_selection();
    void request_amass_conversion();
    std::optional<ConverterCommand> prepare_amass_conversion();
    void start_converter_process(const ConverterCommand& command);
    void setup_converter_callbacks();
    void finish_converter_process(int exit_code, QProcess::ExitStatus exit_status);

    ProjectPaths project_paths_;
    std::vector<MotionAsset> motions_;
    QProcess* converter_process_ = nullptr;
    ConverterResult converter_result_;
    QWidget* viewer_container_ = nullptr;
    MotionBrowserPanel* browser_panel_ = nullptr;
    OpenGLViewerWidget* viewer_widget_ = nullptr;
};
