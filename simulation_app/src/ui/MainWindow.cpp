#include "ui/MainWindow.h"

#include "asset/AssetLoader.h"
#include "asset/GarmentAsset.h"
#include "asset/MotionAsset.h"
#include "asset/MotionConverter.h"
#include "simulation/SimulationController.h"
#include "ui/AssetBrowserPanel.h"
#include "ui/SceneViewport.h"
#include "utils/QtUtils.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

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

void show_motion_conversion_failure(QWidget* parent)
{
    QMessageBox::warning(
        parent,
        "Conversion Failed",
        "Failed to convert AMASS motion."
    );
}

void show_garment_conversion_failure(QWidget* parent)
{
    QMessageBox::warning(
        parent,
        "Conversion Failed",
        "Failed to convert garment OBJ."
    );
}

std::vector<std::filesystem::path> make_motion_asset_paths(const std::vector<MotionAsset>& motions)
{
    std::vector<std::filesystem::path> asset_paths;
    asset_paths.reserve(motions.size());
    for (const MotionAsset& motion : motions) {
        asset_paths.push_back(motion.motion_asset_path);
    }
    return asset_paths;
}

std::vector<std::filesystem::path> make_garment_asset_paths(const std::vector<GarmentAsset>& garments)
{
    std::vector<std::filesystem::path> asset_paths;
    asset_paths.reserve(garments.size());
    for (const GarmentAsset& garment : garments) {
        asset_paths.push_back(garment.garment_asset_path);
    }
    return asset_paths;
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
    simulation_controller_ = std::make_unique<SimulationController>();
    browser_panel_ = new AssetBrowserPanel(viewer_container_);
    asset_loader_ = new AssetLoader(this);
    motion_converter_ = new MotionConverter(this);
    garment_converter_ = new MotionConverter(this);

    setCentralWidget(viewer_container_);
    viewer_container_->installEventFilter(this);

    browser_panel_->raise();
    setup_callbacks();

    refresh_motion_list();
    refresh_garment_list();
    update_viewer_layout();
}

MainWindow::~MainWindow()
{
    if (simulation_controller_) {
        simulation_controller_->release_gpu();
        simulation_controller_->set_viewport_callbacks({});
    }
    if (simulation_viewport_) {
        simulation_viewport_->set_initialize_callback({});
        simulation_viewport_->set_scene_render_callback({});
    }
}

void MainWindow::setup_callbacks()
{
    simulation_controller_->set_viewport_callbacks({
        [this]() {
            return simulation_viewport_ != nullptr && simulation_viewport_->is_gl_initialized();
        },
        [this](SimulationController::GlContextTask task) {
            if (simulation_viewport_ == nullptr) {
                return;
            }

            run_with_gl_context(*simulation_viewport_, [&] {
                task(simulation_viewport_->gl_functions());
            });
        },
        [this]() {
            if (simulation_viewport_ != nullptr) {
                simulation_viewport_->update();
            }
        },
        [this](const CharacterMesh& character_mesh) {
            if (simulation_viewport_ != nullptr) {
                simulation_viewport_->reset_camera_to_character(character_mesh);
            }
        },
    });
    simulation_viewport_->set_initialize_callback(
        [this](QOpenGLFunctions_4_5_Core& gl) {
            return simulation_controller_->initialize_gpu(project_paths_.shaders, gl);
        }
    );
    simulation_viewport_->set_scene_render_callback(
        [this](const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl) {
            if (simulation_controller_ != nullptr && simulation_controller_->is_gpu_initialized()) {
                simulation_controller_->draw(mvp, gl);
            }
        }
    );

    browser_panel_->set_selected_callback(
        [this](AssetPanelMode mode, const std::filesystem::path& asset_path) {
            if (mode == AssetPanelMode::Motions) {
                asset_loader_->load_character_mesh(asset_path);
                return;
            }
            asset_loader_->load_garment_mesh(asset_path);
        }
    );
    browser_panel_->set_import_button_callback([this](AssetPanelMode mode) {
        if (mode == AssetPanelMode::Motions) {
            request_amass_conversion();
            return;
        }
        request_garment_conversion();
    });
    browser_panel_->set_expansion_changed_callback([this]() {
        update_viewer_layout();
    });

    asset_loader_->set_character_loaded_callback(
        [this](const std::filesystem::path&, CharacterMesh mesh) {
            simulation_controller_->set_character_mesh(std::move(mesh));
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

    motion_converter_->set_conversion_succeeded_callback([this]() {
        browser_panel_->set_conversion_active(AssetPanelMode::Motions, false);
        refresh_motion_list();
    });
    motion_converter_->set_conversion_failed_callback([this](const std::string& error_message) {
        browser_panel_->set_conversion_active(AssetPanelMode::Motions, false);
        std::cerr << error_message << '\n';
        show_motion_conversion_failure(this);
    });

    garment_converter_->set_conversion_succeeded_callback([this]() {
        browser_panel_->set_conversion_active(AssetPanelMode::Garments, false);
        refresh_garment_list();
    });
    garment_converter_->set_conversion_failed_callback([this](const std::string& error_message) {
        browser_panel_->set_conversion_active(AssetPanelMode::Garments, false);
        std::cerr << error_message << '\n';
        show_garment_conversion_failure(this);
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

void MainWindow::refresh_motion_list()
{
    browser_panel_->set_asset_paths(
        AssetPanelMode::Motions,
        make_motion_asset_paths(scan_motion_assets(project_paths_))
    );
}

void MainWindow::refresh_garment_list()
{
    browser_panel_->set_asset_paths(
        AssetPanelMode::Garments,
        make_garment_asset_paths(scan_garment_assets(project_paths_))
    );
}

void MainWindow::request_amass_conversion()
{
    if (motion_converter_->is_running()) {
        return;
    }

    const std::optional<ConverterCommand> command = prepare_amass_conversion();
    if (!command) {
        return;
    }

    browser_panel_->set_conversion_active(AssetPanelMode::Motions, true);
    motion_converter_->start_conversion(*command);
}

void MainWindow::request_garment_conversion()
{
    if (garment_converter_->is_running()) {
        return;
    }

    const std::optional<ConverterCommand> command = prepare_garment_conversion();
    if (!command) {
        return;
    }

    browser_panel_->set_conversion_active(AssetPanelMode::Garments, true);
    garment_converter_->start_conversion(*command);
}

std::optional<ConverterCommand> MainWindow::prepare_amass_conversion()
{
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

    if (std::filesystem::exists(motion_asset_path)) {
        refresh_motion_list();
        return std::nullopt;
    }

    const ConverterCommand command = make_converter_command(project_paths_, amass_motion_path, motion_asset_path);
    if (!command.is_valid) {
        std::cerr << command.error_message << '\n';
        show_motion_conversion_failure(this);
        return std::nullopt;
    }

    return command;
}

std::optional<ConverterCommand> MainWindow::prepare_garment_conversion()
{
    const std::filesystem::path source_dir = project_paths_.garment_asset_dir / "source";
    const std::filesystem::path default_dir = std::filesystem::exists(source_dir)
        ? source_dir
        : project_paths_.garment_asset_dir;
    const QString selected_file = QFileDialog::getOpenFileName(
        this,
        "Select Garment OBJ",
        to_q_string(default_dir),
        "Garment OBJ (*.obj)"
    );

    if (selected_file.isEmpty()) {
        return std::nullopt;
    }

    const std::filesystem::path garment_obj_path = selected_file.toStdWString();
    const std::filesystem::path garment_asset_path =
        project_paths_.garment_asset_dir / (garment_obj_path.stem().string() + ".garment");

    if (std::filesystem::exists(garment_asset_path)) {
        refresh_garment_list();
        return std::nullopt;
    }

    const ConverterCommand command =
        make_garment_converter_command(project_paths_, garment_obj_path, garment_asset_path);
    if (!command.is_valid) {
        std::cerr << command.error_message << '\n';
        show_garment_conversion_failure(this);
        return std::nullopt;
    }

    return command;
}
