#include "ui/MainWindow.h"

#include "asset/AssetConverter.h"
#include "asset/AssetConverterCommands.h"
#include "asset/AssetIO.h"
#include "asset/AssetLoader.h"
#include "simulation/SimulationController.h"
#include "ui/AssetBrowserPanel.h"
#include "ui/GarmentPlacementPanel.h"
#include "ui/SceneViewport.h"
#include "utils/QtUtils.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <utility>

#include <QEvent>
#include <QFileDialog>
#include <QIcon>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSize>
#include <QPixmap>
#include <QWidget>

namespace {
constexpr int initial_window_width = 1440;
constexpr int initial_window_height = 900;
constexpr int panel_margin = 12;
constexpr int panel_width = 340;
constexpr int panel_min_height = 180;
constexpr int panel_max_height = 280;
constexpr int simulation_button_size = 40;
constexpr int simulation_button_gap = 10;
constexpr int simulation_button_count = 3;
constexpr int simulation_icon_size = 22;
constexpr int placement_panel_width = 280;
constexpr int placement_panel_height = 210;

enum class SimulationControlIcon {
    Play,
    Pause,
    Reset,
};

QIcon make_simulation_control_icon(SimulationControlIcon icon_type, const QColor& icon_color)
{
    QPixmap pixmap(simulation_icon_size, simulation_icon_size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(icon_color);

    switch (icon_type) {
    case SimulationControlIcon::Play: {
        QPolygonF triangle;
        triangle << QPointF{6.0, 4.0} << QPointF{6.0, 18.0} << QPointF{18.0, 11.0};
        painter.drawPolygon(triangle);
        break;
    }
    case SimulationControlIcon::Pause:
        painter.drawRect(QRectF{6.0, 4.0, 4.5, 14.0});
        painter.drawRect(QRectF{13.5, 4.0, 4.5, 14.0});
        break;
    case SimulationControlIcon::Reset:
        painter.drawRect(QRectF{5.0, 5.0, 12.0, 12.0});
        break;
    }

    return QIcon{pixmap};
}

void configure_simulation_button(
    QPushButton* button,
    SimulationControlIcon icon_type,
    const char* tool_tip,
    const QColor& icon_color
)
{
    button->setText("");
    button->setFixedSize(simulation_button_size, simulation_button_size);
    button->setIcon(make_simulation_control_icon(icon_type, icon_color));
    button->setIconSize(QSize{simulation_icon_size, simulation_icon_size});
    button->setToolTip(tool_tip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setStyleSheet(
        "QPushButton {"
        "  background-color: #eeeeee;"
        "  border: 1px solid #c8c8c8;"
        "  border-radius: 7px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #f7f7f7;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #dddddd;"
        "  border-color: #c6c6c6;"
        "}"
    );
}

void show_conversion_failure(QWidget* parent, AssetPanelMode mode)
{
    const char* message = mode == AssetPanelMode::Motions
        ? "Failed to convert AMASS motion."
        : "Failed to convert garment OBJ.";

    QMessageBox::warning(parent, "Conversion Failed", message);
}

std::optional<ConverterCommand> validate_conversion_command(QWidget* parent, AssetPanelMode mode, ConverterCommand command)
{
    if (command.is_valid) {
        return command;
    }

    std::cerr << command.error_message << '\n';
    show_conversion_failure(parent, mode);
    return std::nullopt;
}
}

MainWindow::MainWindow(const std::filesystem::path& project_root, QWidget* parent)
    : QMainWindow(parent), project_paths_(make_project_paths(project_root))
{
    setWindowTitle("SIMULATION APP");
    resize(initial_window_width, initial_window_height);

    viewer_container_ = new QWidget(this);
    simulation_viewport_ = new SceneViewport(viewer_container_);
    simulation_controller_ = std::make_unique<SimulationController>();
    browser_panel_ = new AssetBrowserPanel(viewer_container_);
    garment_placement_panel_ = new GarmentPlacementPanel(viewer_container_);
    run_button_ = new QPushButton(viewer_container_);
    stop_button_ = new QPushButton(viewer_container_);
    reset_button_ = new QPushButton(viewer_container_);
    asset_loader_ = new AssetLoader(this);
    motion_converter_ = new AssetConverter(this);
    garment_converter_ = new AssetConverter(this);

    configure_simulation_button(run_button_,
                                SimulationControlIcon::Play,
                                "Run",
                                QColor{"#43a047"});
    configure_simulation_button(stop_button_,
                                SimulationControlIcon::Pause,
                                "Stop",
                                QColor{"#f4b400"});
    configure_simulation_button(reset_button_,
                                SimulationControlIcon::Reset,
                                "Reset",
                                QColor{"#e53935"});

    setCentralWidget(viewer_container_);
    viewer_container_->installEventFilter(this);

    setup_callbacks();

    refresh_motion_list();
    refresh_garment_list();
    update_simulation_controls();
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

bool MainWindow::initialize_scene(QOpenGLFunctions_4_5_Core& gl)
{
    CharacterMesh default_character_mesh;
    if (!asset_io::read_character_mesh_asset(project_paths_.default_character_motion_path, default_character_mesh)) {
        return false;
    }

    if (!simulation_controller_->initialize_gpu(project_paths_.shaders, gl)) {
        return false;
    }

    simulation_controller_->load_default_character_mesh(std::move(default_character_mesh), gl);
    return true;
}

// callback //
void MainWindow::setup_callbacks()
{
    setup_viewport_callbacks();
    setup_browser_callbacks();
    setup_asset_loader_callbacks();
    setup_asset_converter_callbacks();
}

void MainWindow::setup_viewport_callbacks()
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
            return initialize_scene(gl);
        }
    );
    simulation_viewport_->set_scene_render_callback(
        [this](const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl) {
            if (simulation_controller_ != nullptr && simulation_controller_->is_gpu_initialized()) {
                simulation_controller_->draw(mvp, gl);
            }
        }
    );
}

void MainWindow::setup_browser_callbacks()
{
    browser_panel_->set_selected_callback(
        [this](AssetPanelMode mode, const std::filesystem::path& asset_path) {
            if (simulation_controller_->is_simulation_running()) {
                QMessageBox::information(this, "Asset Load Blocked", "Stop the simulation before loading an asset.");
                return;
            }

            if (mode == AssetPanelMode::Motions) {
                asset_loader_->load_character_mesh(asset_path);
                return;
            } else {
                if (!simulation_controller_->is_default_pose()) {
                    QMessageBox::information(this, "Garment Load Blocked", "Load garments only from the default pose.");
                    return;
                }
                asset_loader_->load_garment_mesh(asset_path);
            }

        }
    );
    browser_panel_->set_import_button_callback([this](AssetPanelMode mode) {
        request_conversion(mode);
    });
    browser_panel_->set_expansion_changed_callback([this]() {
        update_viewer_layout();
    });
    garment_placement_panel_->set_placement_changed_callback(
        [this](const glm::vec3& position_offset, float scale) {
            simulation_controller_->set_garment_placement(position_offset, scale);
        }
    );

    connect(run_button_, &QPushButton::clicked, this, [this]() {
        simulation_controller_->start_simulation();
        update_simulation_controls();
    });

    connect(stop_button_, &QPushButton::clicked, this, [this]() {
        simulation_controller_->stop_simulation();
        update_simulation_controls();
    });

    connect(reset_button_, &QPushButton::clicked, this, [this]() {
        simulation_controller_->reset_scene_to_default();
        has_editable_garment_ = false;
        garment_placement_panel_->reset_placement();
        update_simulation_controls();
        update_viewer_layout();
    });

    garment_placement_panel_->set_confirm_run_callback([this]() {
        simulation_controller_->start_simulation();
        has_editable_garment_ = false;
        update_simulation_controls();
        update_viewer_layout();
    });
}

void MainWindow::setup_asset_loader_callbacks()
{
    asset_loader_->set_character_loaded_callback(
        [this](const std::filesystem::path&, CharacterMesh mesh) {
            has_editable_garment_ = false;
            simulation_controller_->set_character_mesh(std::move(mesh));
            update_simulation_controls();
        }
    );
    asset_loader_->set_character_load_failed_callback([this](const std::filesystem::path& motion_asset_path) {
        QMessageBox::warning(this, "Load Failed", "Failed to load motion:\n" + to_q_string(motion_asset_path));
    });
    asset_loader_->set_garment_loaded_callback([this](GarmentMesh mesh) {
        simulation_controller_->add_garment_mesh(std::move(mesh));
        has_editable_garment_ = true;
        garment_placement_panel_->reset_placement();
        update_simulation_controls();
        update_viewer_layout();
    });
    asset_loader_->set_garment_load_failed_callback([this](const std::filesystem::path& garment_asset_path) {
        QMessageBox::warning(this, "Load Failed", "Failed to load garment:\n" + to_q_string(garment_asset_path));
    });
}

void MainWindow::setup_asset_converter_callbacks()
{
    motion_converter_->set_conversion_succeeded_callback([this]() {
        browser_panel_->set_conversion_active(AssetPanelMode::Motions, false);
        refresh_motion_list();
    });
    motion_converter_->set_conversion_failed_callback([this](const std::string& error_message) {
        browser_panel_->set_conversion_active(AssetPanelMode::Motions, false);
        std::cerr << error_message << '\n';
        show_conversion_failure(this, AssetPanelMode::Motions);
    });

    garment_converter_->set_conversion_succeeded_callback([this]() {
        browser_panel_->set_conversion_active(AssetPanelMode::Garments, false);
        refresh_garment_list();
    });
    garment_converter_->set_conversion_failed_callback([this](const std::string& error_message) {
        browser_panel_->set_conversion_active(AssetPanelMode::Garments, false);
        std::cerr << error_message << '\n';
        show_conversion_failure(this, AssetPanelMode::Garments);
    });
}

// resize event handling //
bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == viewer_container_ && event->type() == QEvent::Resize) {
        update_viewer_layout();
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::update_viewer_layout()
{
    if (!viewer_container_ || !simulation_viewport_ || !browser_panel_ || !garment_placement_panel_) {
        return;
    }

    const QSize container_size = viewer_container_->size();
    simulation_viewport_->setGeometry(0, 0, container_size.width(), container_size.height());

    const QSize collapsed_size = browser_panel_->sizeHint();
    const int available_width = std::max(0, container_size.width() - panel_margin * 2);
    const int available_height = std::max(0, container_size.height() - panel_margin * 2);

    int overlay_width = std::min(collapsed_size.width(), available_width);
    int overlay_height = std::min(collapsed_size.height(), available_height);

    if (browser_panel_->is_expanded()) {
        overlay_width = std::min(panel_width, available_width);

        const int target_height = container_size.height() / 4;
        const int expanded_max_height = std::min(panel_max_height, available_height);
        const int expanded_height = std::clamp(
            target_height,
            std::min(panel_min_height, expanded_max_height),
            expanded_max_height
        );
        overlay_height = expanded_height;
    }

    browser_panel_->setGeometry(panel_margin, panel_margin, overlay_width, overlay_height);
    browser_panel_->raise();

    const int controls_width =
        simulation_button_size * simulation_button_count + simulation_button_gap * (simulation_button_count - 1);
    const int controls_x = std::max(panel_margin, (container_size.width() - controls_width) / 2);
    const int controls_y = panel_margin;
    run_button_->setGeometry(controls_x,
                             controls_y,
                             simulation_button_size,
                             simulation_button_size);
    stop_button_->setGeometry(controls_x + simulation_button_size + simulation_button_gap,
                              controls_y,
                              simulation_button_size,
                              simulation_button_size);
    reset_button_->setGeometry(controls_x + (simulation_button_size + simulation_button_gap) * 2,
                               controls_y,
                               simulation_button_size,
                               simulation_button_size);
    run_button_->raise();
    stop_button_->raise();
    reset_button_->raise();

    if (has_editable_garment_) {
        const int placement_width = std::min(placement_panel_width, available_width);
        const int placement_height = std::min(placement_panel_height, available_height);
        const int placement_x = std::max(panel_margin, container_size.width() - panel_margin - placement_width);
        const int placement_y = controls_y + simulation_button_size + simulation_button_gap;
        garment_placement_panel_->setGeometry(placement_x, placement_y, placement_width, placement_height);
        garment_placement_panel_->raise();
    }
}

// simulation control //
void MainWindow::update_simulation_controls()
{
    if (!simulation_controller_ || !run_button_ || !stop_button_ || !reset_button_ || !garment_placement_panel_) {
        return;
    }

    const bool simulation_running = simulation_controller_->is_simulation_running();
    const bool placement_available =
        has_editable_garment_ &&
        !simulation_running &&
        simulation_controller_->is_default_pose();

    const bool placement_panel_visible = has_editable_garment_;

    run_button_->setEnabled(!simulation_running && !placement_panel_visible);
    stop_button_->setEnabled(simulation_running);
    reset_button_->setEnabled(true);
    garment_placement_panel_->setVisible(placement_panel_visible);
    garment_placement_panel_->setEnabled(placement_available);
}

// panel update //
void MainWindow::refresh_motion_list()
{
    browser_panel_->set_asset_paths(
        AssetPanelMode::Motions,
        asset_io::scan_motion_asset_paths(project_paths_)
    );
}

void MainWindow::refresh_garment_list()
{
    browser_panel_->set_asset_paths(
        AssetPanelMode::Garments,
        asset_io::scan_garment_asset_paths(project_paths_)
    );
}

// converter request //
void MainWindow::request_conversion(AssetPanelMode mode)
{
    AssetConverter* converter = mode == AssetPanelMode::Motions
        ? motion_converter_
        : garment_converter_;

    if (converter->is_running()) {
        QMessageBox::information(
            this,
            "Conversion In Progress",
            "변환이 진행 중입니다. 잠시 후 다시 시도해주세요."
        );
        return;
    }

    const std::optional<ConverterCommand> command = (mode == AssetPanelMode::Motions)
        ? prepare_amass_conversion()
        : prepare_garment_conversion();

    if (!command) {
        return;
    }

    browser_panel_->set_conversion_active(mode, true);
    converter->start_conversion(*command);
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
    const std::filesystem::path motion_asset_path = asset_io::make_motion_asset_path(project_paths_, amass_motion_path);

    if (std::filesystem::exists(motion_asset_path)) {
        return std::nullopt;
    }

    const auto command = asset_converter_commands::make_motion_command(project_paths_, amass_motion_path,motion_asset_path);
    return validate_conversion_command(
        this,
        AssetPanelMode::Motions,
        command
    );
}

std::optional<ConverterCommand> MainWindow::prepare_garment_conversion()
{
    const std::filesystem::path default_dir = project_paths_.garment_source_dir;
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
    const std::filesystem::path garment_asset_path = asset_io::make_garment_asset_path(project_paths_, garment_obj_path);

    if (std::filesystem::exists(garment_asset_path)) {
        return std::nullopt;
    }

    const auto command = asset_converter_commands::make_garment_command(project_paths_, garment_obj_path, garment_asset_path);
    return validate_conversion_command(
        this,
        AssetPanelMode::Garments,
        command
    );
}
