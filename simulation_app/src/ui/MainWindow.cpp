#include "ui/MainWindow.h"

#include "asset/AssetLoader.h"
#include "asset/MotionConverter.h"
#include "simulation/SimulationController.h"
#include "ui/SceneViewport.h"
#include "support/QtHelpers.h"
#include "ui/MotionBrowserPanel.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <utility>

#include <QEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QWidget>

namespace {
int clamp_int(int value, int min_value, int max_value)
{
    return std::max(min_value, std::min(value, max_value));
}

void show_conversion_failure(QWidget* parent)
{
    QMessageBox::warning(
        parent,
        "Conversion Failed",
        "Failed to convert AMASS motion."
    );
}

}

MainWindow::MainWindow(const std::filesystem::path& project_root, QWidget* parent)
    : QMainWindow(parent)
    , project_paths_(make_project_paths(project_root))
{
    setWindowTitle("SIMULATION APP");
    resize(1440, 900);

    viewer_container_ = new QWidget(this);
    simulation_viewport_ = new SceneViewport(viewer_container_);
    simulation_controller_ = std::make_unique<SimulationController>(*simulation_viewport_);
    simulation_viewport_->set_controller(simulation_controller_.get());
    browser_panel_ = new MotionBrowserPanel(viewer_container_);
    asset_loader_ = new AssetLoader(this);
    motion_converter_ = new MotionConverter(this);

    setCentralWidget(viewer_container_);
    viewer_container_->installEventFilter(this);
    
    // motion 선택 toggle 관련 event callback 함수 설정
    browser_panel_->raise();
    setup_callbacks();

    refresh_motion_list();
    update_viewer_layout();
}

MainWindow::~MainWindow()
{
    if (simulation_controller_) {
        simulation_controller_->release_gpu();
    }
    if (simulation_viewport_) {
        simulation_viewport_->set_controller(nullptr);
    }
}

// Layout //

void MainWindow::setup_callbacks()
{
    // Browser panel callbacks
    browser_panel_->set_motion_selected_callback([this](const std::filesystem::path& motion_asset_path) {
        asset_loader_->load_character_mesh(motion_asset_path);
    });
    browser_panel_->set_motion_import_button_callback([this]() {
        request_amass_conversion();
    });
    browser_panel_->set_garment_button_callback([this]() {
        request_garment_asset_selection();
    });
    browser_panel_->set_expansion_changed_callback([this]() {
        update_viewer_layout();
    });

    // Asset loader callbacks
    asset_loader_->set_character_loaded_callback(
        [this](const std::filesystem::path& motion_asset_path, CharacterMesh mesh) {
            simulation_controller_->set_character_mesh(std::move(mesh));
            browser_panel_->set_current_motion_asset(motion_asset_path);
        }
    );
    asset_loader_->set_character_load_failed_callback([this](const std::filesystem::path& motion_asset_path) {
        QMessageBox::warning(this, "Load Failed", "Failed to load motion:\n" + to_q_string(motion_asset_path));
    });
    asset_loader_->set_garment_loaded_callback([this](GarmentMesh mesh) {
        simulation_controller_->add_garment_mesh(std::move(mesh));
    });
    asset_loader_->set_garment_load_failed_callback([this](const std::filesystem::path& garment_asset_path) {
        QMessageBox::warning(this, "Load Failed", "Failed to load garment:\n" + to_q_string(garment_asset_path));
    });

    // Motion converter callbacks
    motion_converter_->set_conversion_succeeded_callback([this]() {
        browser_panel_->set_conversion_active(false);
        refresh_motion_list();
    });
    motion_converter_->set_conversion_failed_callback([this](const std::string& error_message) {
        browser_panel_->set_conversion_active(false);
        std::cerr << error_message << '\n';
        show_conversion_failure(this);
    });
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == viewer_container_ && event->type() == QEvent::Resize) {
        update_viewer_layout();
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::update_viewer_layout()
{
    if (!viewer_container_ || !simulation_viewport_ || !browser_panel_) {
        return;
    }

    const QSize container_size = viewer_container_->size();
    simulation_viewport_->setGeometry(QRect(QPoint(0, 0), container_size));

    constexpr int margin = 12;
    constexpr int expanded_width = 340;
    constexpr int min_expanded_height = 180;
    constexpr int max_expanded_height = 280;

    const QSize collapsed_size = browser_panel_->sizeHint();
    const int available_width = std::max(0, container_size.width() - margin * 2);
    const int available_height = std::max(0, container_size.height() - margin * 2);

    int overlay_width = std::min(collapsed_size.width(), available_width);
    int overlay_height = std::min(collapsed_size.height(), available_height);

    if (browser_panel_->is_expanded()) {
        overlay_width = std::min(expanded_width, available_width);

        const int target_height = container_size.height() / 4;
        const int expanded_max_height = std::min(max_expanded_height, available_height);
        const int expanded_height = clamp_int(
            target_height,
            std::min(min_expanded_height, expanded_max_height),
            expanded_max_height
        );
        overlay_height = expanded_height;
    }

    browser_panel_->setGeometry(margin, margin, overlay_width, overlay_height);
    browser_panel_->raise();
}


// Motion assets

void MainWindow::refresh_motion_list()
{
    motions_ = scan_motion_assets(project_paths_);
    browser_panel_->set_motion_list(motions_);
}

void MainWindow::request_garment_asset_selection()
{
    const QString selected_file = QFileDialog::getOpenFileName(
        this,
        "Select Garment",
        to_q_string(project_paths_.garment_asset_dir),
        "Garment OBJ (*.obj);;All Files (*)"
    );

    if (selected_file.isEmpty()) {
        return;
    }

    asset_loader_->queue_garment_mesh_load(selected_file.toStdWString());
}

// AMASS conversion //

// AMASS motion 변환 요청
void MainWindow::request_amass_conversion()
{
    // 중복 실행 방지: 현재 변환 작업 중이면 종료
    if (motion_converter_->is_running()) {
        return;
    }

    // python converter 실행에 필요한 command 준비
    const std::optional<ConverterCommand> command = prepare_amass_conversion();
    if (!command) {
        return;
    }

    // 변환 작업 시작
    browser_panel_->set_conversion_active(true);
    motion_converter_->start_conversion(*command);
}

std::optional<ConverterCommand> MainWindow::prepare_amass_conversion()
{   
    // 파일 선택 창을 열고 motion 선택
    const std::filesystem::path default_dir = project_paths_.amass_dir;
    const QString selected_file = QFileDialog::getOpenFileName(
        this,
        "Select AMASS Motion",
        to_q_string(default_dir),
        "AMASS Motion (*.npz)"
    );

    if (selected_file.isEmpty()) {
        return std::nullopt;
    }

    const std::filesystem::path amass_motion_path = selected_file.toStdWString();
    const std::filesystem::path motion_asset_path = make_motion_asset_path(project_paths_, amass_motion_path);


    // 이미 변환된 motion이면 변환하지 않음
    if (std::filesystem::exists(motion_asset_path)) {
        refresh_motion_list();
        return std::nullopt;
    }

    // python converter 실행에 필요한 command 준비
    const ConverterCommand command = make_converter_command(project_paths_, amass_motion_path, motion_asset_path);
    if (!command.is_valid) {
        std::cerr << command.error_message << '\n';
        show_conversion_failure(this);
        return std::nullopt;
    }

    return command;
}
