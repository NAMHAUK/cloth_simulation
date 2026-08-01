#include "ui/MainWindow.h"

#include "asset/AssetConverter.h"
#include "asset/AssetConverterCommands.h"
#include "asset/AssetIO.h"
#include "asset/AssetLoader.h"
#include "simulation/SimulationController.h"
#include "ui/AssetBrowserPanel.h"
#include "ui/GarmentColorPanel.h"
#include "ui/GarmentPlacementPanel.h"
#include "ui/SceneViewport.h"
#include "utils/QtUtils.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSize>
#include <QVBoxLayout>
#include <QWidget>

namespace {
constexpr int initial_window_width = 1440;
constexpr int initial_window_height = 900;
constexpr int minimum_window_width = 1000;
constexpr int minimum_window_height = 600;
constexpr int panel_margin = 12;
constexpr int panel_width = 390;
constexpr int panel_min_height = 270;
constexpr int panel_max_height = 540;
constexpr int simulation_button_size = 40;
constexpr int simulation_button_gap = 10;
constexpr int simulation_button_count = 3;
constexpr int simulation_icon_size = 22;
constexpr int placement_panel_width = 280;
constexpr int garment_color_button_size = 38;
constexpr int garment_card_height = garment_color_button_size;
constexpr float placement_character_opacity = 0.3f;

int motion_subject_number(const std::filesystem::path& motion_path)
{
    bool is_number = false;
    const int subject_number = to_q_string(motion_path.stem()).section('_', 0, 0).toInt(&is_number);
    return is_number ? subject_number : std::numeric_limits<int>::max();
}

bool is_amass_motion_path(const std::filesystem::path& motion_path)
{
    const QString motion_name = to_q_string(motion_path.stem());
    return motion_name.size() > 6 && motion_name.endsWith("_poses");
}

enum class SimulationControlIcon
{
    Play,
    Pause,
    DefaultPose,
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
    case SimulationControlIcon::DefaultPose: {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen{icon_color, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});
        painter.drawArc(QRectF{4.0, 3.5, 14.0, 14.0}, -35 * 16, 285 * 16);

        painter.setPen(Qt::NoPen);
        painter.setBrush(icon_color);
        QPolygonF arrow;
        arrow << QPointF{4.2, 7.0} << QPointF{4.2, 2.5} << QPointF{8.6, 6.8};
        painter.drawPolygon(arrow);
        break;
    }
    case SimulationControlIcon::Reset:
        painter.drawRect(QRectF{5.0, 5.0, 12.0, 12.0});
        break;
    }

    return QIcon{pixmap};
}

void configure_simulation_button(QPushButton* button,
                                 SimulationControlIcon icon_type,
                                 const char* tool_tip,
                                 const QColor& icon_color)
{
    button->setText("");
    button->setFixedSize(simulation_button_size, simulation_button_size);
    button->setIcon(make_simulation_control_icon(icon_type, icon_color));
    button->setIconSize(QSize{simulation_icon_size, simulation_icon_size});
    button->setToolTip(tool_tip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setStyleSheet("QPushButton {"
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
                          "}");
}

void show_conversion_failure(QWidget* parent, AssetPanelMode mode)
{
    const char* message = mode == AssetPanelMode::Motions ? "Failed to convert AMASS motion."
                                                          : "Failed to convert garment OBJ.";

    QMessageBox::warning(parent, "Conversion Failed", message);
}

std::optional<ConverterCommand> validate_conversion_command(QWidget* parent,
                                                            AssetPanelMode mode,
                                                            ConverterCommand command)
{
    if (command.is_valid) {
        return command;
    }

    std::cerr << command.error_message << '\n';
    show_conversion_failure(parent, mode);
    return std::nullopt;
}

struct GarmentConversionSettings final
{
    QString attachment_type;
    QString garment_category;
};

std::optional<GarmentConversionSettings> select_garment_conversion_settings(QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle("Garment Conversion Settings");

    QComboBox attachment_type;
    attachment_type.addItem("None", "none");
    attachment_type.addItem("Waistband", "waistband");

    QComboBox garment_category;
    garment_category.addItem("Top", "top");
    garment_category.addItem("Bottom", "bottom");
    garment_category.addItem("Full-Body", "full-body");

    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QFormLayout layout(&dialog);
    layout.addRow("Attachment Type", &attachment_type);
    layout.addRow("Garment Category", &garment_category);
    layout.addRow(&buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    return GarmentConversionSettings{attachment_type.currentData().toString(),
                                     garment_category.currentData().toString()};
}
}

MainWindow::MainWindow(const std::filesystem::path& project_root, QWidget* parent)
    : QMainWindow(parent),
      project_paths_(make_project_paths(project_root))
{
    setWindowTitle("SIMULATION APP");
    setMinimumSize(minimum_window_width, minimum_window_height);
    resize(initial_window_width, initial_window_height);

    viewer_container_ = new QWidget(this);
    simulation_viewport_ = new SceneViewport(viewer_container_);
    simulation_controller_ = std::make_unique<SimulationController>();
    browser_panel_ = new AssetBrowserPanel(project_paths_.motion_asset_dir / "motion_catalog.json",
                                           project_paths_.motion_asset_dir / "subject_catalog.json",
                                           viewer_container_);
    garment_placement_panel_ = new GarmentPlacementPanel(viewer_container_);
    garment_color_panel_ = new GarmentColorPanel(viewer_container_);
    create_garment_cards();
    run_button_ = new QPushButton(viewer_container_);
    stop_button_ = new QPushButton(viewer_container_);
    reset_button_ = new QPushButton(viewer_container_);
    asset_loader_ = new AssetLoader(this);
    motion_converter_ = new AssetConverter(this);
    garment_converter_ = new AssetConverter(this);

    configure_simulation_button(run_button_, SimulationControlIcon::Play, "Run", QColor{"#43a047"});
    configure_simulation_button(stop_button_,
                                SimulationControlIcon::DefaultPose,
                                "Default Pose",
                                QColor{"#1e88e5"});
    configure_simulation_button(reset_button_, SimulationControlIcon::Reset, "Reset", QColor{"#e53935"});

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
    std::vector<std::uint8_t> default_triangle_part_labels;
    if (!asset_io::read_default_character_mesh(project_paths_.default_character_motion_path,
                                               default_character_mesh,
                                               default_triangle_part_labels)) {
        return false;
    }

    if (!simulation_controller_->initialize_gpu(project_paths_.shaders, gl)) {
        return false;
    }

    simulation_controller_->load_default_character_mesh(std::move(default_character_mesh),
                                                        default_triangle_part_labels,
                                                        gl);
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
        [this]() { return simulation_viewport_ != nullptr && simulation_viewport_->is_gl_initialized(); },
        [this](SimulationController::GlContextTask task) {
            if (simulation_viewport_ == nullptr) {
                return;
            }

            run_with_gl_context(*simulation_viewport_, [&] { task(simulation_viewport_->gl_functions()); });
        },
        [this]() {
            if (simulation_viewport_ != nullptr) {
                simulation_viewport_->update();
            }
        },
        [this](const glm::vec3& root_position) {
            if (simulation_viewport_ != nullptr) {
                simulation_viewport_->reset_camera_to_character_root(root_position);
            }
        },
        [this](const glm::vec3& root_position) {
            if (simulation_viewport_ != nullptr) {
                simulation_viewport_->set_camera_target(root_position);
            }
        },
    });
    simulation_viewport_->set_initialize_callback(
        [this](QOpenGLFunctions_4_5_Core& gl) { return initialize_scene(gl); });
    simulation_viewport_->set_scene_render_callback(
        [this](const glm::mat4& mvp, QOpenGLFunctions_4_5_Core& gl) {
            if (simulation_controller_ != nullptr && simulation_controller_->is_gpu_initialized()) {
                const float character_opacity = has_placement_session() ? placement_character_opacity : 1.0f;
                simulation_controller_->draw(mvp, character_opacity, gl);
            }
        });
}

void MainWindow::setup_browser_callbacks()
{
    browser_panel_->set_selected_callback(
        [this](AssetPanelMode mode, const std::filesystem::path& asset_path) {
            if (mode == AssetPanelMode::Motions) {
                asset_loader_->load_character_mesh(asset_path);
                return;
            } else {
                if (simulation_controller_->is_simulation_running() ||
                    !simulation_controller_->is_default_pose()) {
                    simulation_controller_->return_to_default_pose();
                }
                request_garment_load(asset_path);
            }
        });
    browser_panel_->set_motion_conversion_callback(
        [this](const std::filesystem::path& source_path) { request_motion_conversion(source_path); });
    browser_panel_->set_import_button_callback([this]() { request_garment_conversion(); });
    browser_panel_->set_expansion_changed_callback([this]() { update_viewer_layout(); });
    garment_placement_panel_->set_placement_changed_callback(
        [this](std::size_t group_index, const glm::vec3& position_offset, float scale) {
            simulation_controller_->set_garment_placement(group_index, position_offset, scale);
        });
    garment_placement_panel_->set_color_changed_callback(
        [this](std::size_t group_index, const glm::vec3& color) {
            simulation_controller_->set_garment_color(group_index, color);
            update_garment_card_color(group_index, color);
        });
    garment_placement_panel_->set_color_edit_callback(
        [this](const glm::vec3& color, GarmentPlacementPanel::ColorSelectedCallback callback) {
            highlight_color_edit_card(std::nullopt);
            garment_color_panel_->edit_color(color, std::move(callback));
        });
    garment_color_panel_->set_visibility_changed_callback([this]() {
        if (!garment_color_panel_->isVisible()) {
            highlight_color_edit_card(std::nullopt);
        }
        update_viewer_layout();
    });
    garment_placement_panel_->set_add_upper_callback([this]() {
        placement_group_states_[GarmentPlacementPanel::upper_group_index] = PlacementGroupState::Empty;
        garment_request_ids_[GarmentPlacementPanel::upper_group_index].reset();
        garment_placement_panel_->show_upper_placeholder();
        update_placement_actions();
        update_viewer_layout();
    });
    garment_placement_panel_->set_remove_upper_callback([this]() {
        const std::size_t upper_index = GarmentPlacementPanel::upper_group_index;
        garment_color_panel_->close_panel();
        garment_request_ids_[upper_index].reset();
        simulation_controller_->remove_garment_placement(upper_index);
        clear_garment_card(upper_index);
        placement_group_states_[upper_index] = PlacementGroupState::Hidden;

        if (placement_group_states_[GarmentPlacementPanel::lower_group_index] !=
            PlacementGroupState::Hidden) {
            garment_placement_panel_->remove_upper_group();
            update_simulation_controls();
            update_viewer_layout();
            return;
        }

        end_placement_session();
        update_simulation_controls();
        update_viewer_layout();
    });

    connect(run_button_, &QPushButton::clicked, this, [this]() {
        if (simulation_controller_->is_simulation_running()) {
            simulation_controller_->stop_simulation();
        } else {
            simulation_controller_->start_simulation();
        }
        update_simulation_controls();
    });

    connect(stop_button_, &QPushButton::clicked, this, [this]() {
        simulation_controller_->return_to_default_pose();
        update_simulation_controls();
    });

    connect(reset_button_, &QPushButton::clicked, this, [this]() {
        garment_color_panel_->close_panel();
        simulation_controller_->reset_scene_to_default();
        for (std::size_t index = 0; index < garment_cards_.size(); ++index) {
            clear_garment_card(index);
        }
        end_placement_session();
        update_simulation_controls();
        update_viewer_layout();
    });

    garment_placement_panel_->set_confirm_run_callback([this]() {
        garment_color_panel_->close_panel();
        if (!simulation_controller_->confirm_garment_placement()) {
            QMessageBox::warning(this, "Placement Failed", "Failed to initialize garment placement.");
            update_placement_actions();
            return;
        }
        for (std::size_t index = 0; index < placement_group_states_.size(); ++index) {
            if (placement_group_states_[index] == PlacementGroupState::Loaded) {
                garment_cards_[index].is_confirmed = true;
            }
        }
        simulation_controller_->start_simulation();
        end_placement_session();
        update_simulation_controls();
        update_viewer_layout();
    });
    garment_placement_panel_->set_cancel_callback([this]() {
        garment_color_panel_->close_panel();
        simulation_controller_->cancel_garment_placement();
        clear_placement_garment_cards();
        end_placement_session();
        update_simulation_controls();
        update_viewer_layout();
    });
}

void MainWindow::setup_asset_loader_callbacks()
{
    asset_loader_->set_character_loaded_callback([this](const std::filesystem::path&, CharacterMesh mesh) {
        end_placement_session();
        simulation_controller_->set_character_mesh(std::move(mesh));
        simulation_controller_->start_simulation();
        update_simulation_controls();
    });
    asset_loader_->set_character_load_failed_callback([this](const std::filesystem::path& motion_asset_path) {
        QMessageBox::warning(this,
                             "Load Failed",
                             "Failed to load motion:\n" + to_q_string(motion_asset_path));
    });
    asset_loader_->set_garment_loaded_callback([this](GarmentRequestId request_id,
                                                      const std::filesystem::path& asset_path,
                                                      GarmentMesh mesh) {
        const std::optional<std::size_t> group_index = take_garment_request_group(request_id);
        if (!group_index) {
            return;
        }

        if (!simulation_controller_->set_garment_mesh(*group_index, std::move(mesh))) {
            QMessageBox::warning(this, "Load Failed", "Failed to apply garment:\n" + to_q_string(asset_path));
            if (!has_visible_placement_group()) {
                end_placement_session();
            }
            update_simulation_controls();
            return;
        }

        const bool starts_placement = !has_visible_placement_group();
        placement_group_states_[*group_index] = PlacementGroupState::Loaded;
        const QString garment_name = to_q_string(asset_path.stem());
        const glm::vec3 color = simulation_controller_->garment_placement_color(*group_index);
        set_garment_card(*group_index,
                         simulation_controller_->garment_placement_id(*group_index),
                         garment_name,
                         color);
        if (starts_placement) {
            garment_placement_panel_->begin_session(*group_index, garment_name, color);
        } else {
            garment_placement_panel_->set_group_garment(*group_index, garment_name, color);
        }

        update_simulation_controls();
        update_viewer_layout();
    });
    asset_loader_->set_garment_load_failed_callback(
        [this](GarmentRequestId request_id, const std::filesystem::path& garment_asset_path) {
            if (!take_garment_request_group(request_id)) {
                return;
            }
            QMessageBox::warning(this,
                                 "Load Failed",
                                 "Failed to load garment:\n" + to_q_string(garment_asset_path));
            if (!has_visible_placement_group()) {
                end_placement_session();
            }
            update_simulation_controls();
        });
}

void MainWindow::request_garment_load(const std::filesystem::path& asset_path)
{
    garment_color_panel_->close_panel();
    const std::size_t group_index =
        has_visible_placement_group()                   ? garment_placement_panel_->active_group_index()
        : simulation_controller_->garment_count() == 0u ? GarmentPlacementPanel::lower_group_index
                                                        : GarmentPlacementPanel::upper_group_index;
    garment_request_ids_[group_index] = asset_loader_->load_garment_mesh(asset_path);
    update_simulation_controls();
}

std::optional<std::size_t> MainWindow::take_garment_request_group(std::uint64_t request_id)
{
    for (std::size_t index = 0; index < garment_request_ids_.size(); ++index) {
        if (garment_request_ids_[index] == request_id) {
            garment_request_ids_[index].reset();
            return index;
        }
    }
    return std::nullopt;
}

bool MainWindow::has_pending_garment_load() const
{
    return std::any_of(garment_request_ids_.begin(),
                       garment_request_ids_.end(),
                       [](const std::optional<std::uint64_t>& request_id) { return request_id.has_value(); });
}

bool MainWindow::has_visible_placement_group() const
{
    return std::any_of(placement_group_states_.begin(),
                       placement_group_states_.end(),
                       [](PlacementGroupState state) { return state != PlacementGroupState::Hidden; });
}

bool MainWindow::has_placement_session() const
{
    return has_visible_placement_group() || has_pending_garment_load();
}

void MainWindow::update_placement_actions()
{
    if (!garment_placement_panel_) {
        return;
    }

    const bool has_visible_group = has_visible_placement_group();
    bool all_visible_groups_loaded = true;
    for (PlacementGroupState state : placement_group_states_) {
        if (state == PlacementGroupState::Hidden) {
            continue;
        }
        all_visible_groups_loaded = all_visible_groups_loaded && state == PlacementGroupState::Loaded;
    }

    garment_placement_panel_->set_confirm_enabled(
        has_visible_group && all_visible_groups_loaded && !has_pending_garment_load());
    garment_placement_panel_->set_add_enabled(
        placement_group_states_[GarmentPlacementPanel::lower_group_index] == PlacementGroupState::Loaded &&
        placement_group_states_[GarmentPlacementPanel::upper_group_index] == PlacementGroupState::Hidden &&
        simulation_controller_->garment_count() < 2u);
}

void MainWindow::end_placement_session()
{
    garment_request_ids_.fill(std::nullopt);
    placement_group_states_.fill(PlacementGroupState::Hidden);
    garment_placement_panel_->reset_placement();
    update_garment_cards();
}

void MainWindow::create_garment_cards()
{
    garment_cards_panel_ = new QWidget(viewer_container_);
    auto* cards_layout = new QVBoxLayout(garment_cards_panel_);
    cards_layout->setContentsMargins(0, 0, 0, 0);
    cards_layout->setSpacing(8);

    for (std::size_t index = 0; index < garment_cards_.size(); ++index) {
        GarmentCard& card = garment_cards_[index];
        card.frame = new QFrame(garment_cards_panel_);
        card.frame->setObjectName("garmentCard");
        card.frame->setFixedHeight(garment_card_height);
        card.frame->setStyleSheet("#garmentCard {"
                                  "  background: transparent;"
                                  "  border: none;"
                                  "}"
                                  "#garmentCardBody {"
                                  "  background-color: #3a3a3a;"
                                  "  border: none;"
                                  "  border-radius: 8px;"
                                  "}"
                                  "#garmentCardBody QLabel {"
                                  "  background: transparent;"
                                  "  border: none;"
                                  "  color: white;"
                                  "  font-size: 16px;"
                                  "  font-weight: 600;"
                                  "}");

        auto* card_layout = new QGridLayout(card.frame);
        card_layout->setContentsMargins(0, 0, 0, 0);
        auto* card_body = new QFrame(card.frame);
        card_body->setObjectName("garmentCardBody");
        card_body->setFixedHeight(garment_card_height);
        auto* body_layout = new QHBoxLayout(card_body);
        body_layout->setContentsMargins(garment_color_button_size / 2 + 10, 0, 12, 0);
        auto* body_row_layout = new QHBoxLayout();
        body_row_layout->setContentsMargins(garment_color_button_size / 2, 0, 0, 0);
        body_row_layout->addWidget(card_body);
        card.color_button = new QPushButton(card.frame);
        card.color_button->setFixedSize(garment_color_button_size, garment_color_button_size);
        card.color_button->setToolTip("Choose garment color");
        card.name_label = new QLabel(card_body);
        body_layout->addWidget(card.name_label);
        card_layout->addLayout(body_row_layout, 0, 0);
        card_layout->addWidget(card.color_button, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
        card.color_button->raise();
        cards_layout->addWidget(card.frame);

        connect(card.color_button, &QPushButton::clicked, this, [this, index]() {
            choose_garment_color(index);
        });
    }

    update_garment_cards();
}

void MainWindow::set_garment_card(std::size_t index,
                                  std::uint32_t garment_id,
                                  const QString& garment_name,
                                  const glm::vec3& color)
{
    if (index >= garment_cards_.size()) {
        return;
    }

    GarmentCard& card = garment_cards_[index];
    card.garment_id = garment_id;
    card.is_confirmed = false;
    card.name_label->setText(garment_name);
    card.name_label->setToolTip(garment_name);
    update_garment_card_color(index, color);
    update_garment_cards();
}

void MainWindow::update_garment_card_color(std::size_t index, const glm::vec3& color)
{
    if (index >= garment_cards_.size() || garment_cards_[index].garment_id == 0u) {
        return;
    }

    GarmentCard& card = garment_cards_[index];
    card.color = color;
    const QColor button_color = QColor::fromRgbF(color.r, color.g, color.b);
    const QString border_color =
        color_edit_card_index_.has_value() && *color_edit_card_index_ == index ? "#1f6feb" : "#111111";
    card.color_button->setStyleSheet(
        QString("QPushButton { background-color: rgb(%1, %2, %3); border: 3px solid %4;"
                " border-radius: 19px; padding: 0; }"
                "QPushButton:hover { border-color: #1f6feb; }"
                "QPushButton:disabled { background-color: rgb(%1, %2, %3); border-color: #666666; }")
            .arg(button_color.red())
            .arg(button_color.green())
            .arg(button_color.blue())
            .arg(border_color));
}

void MainWindow::update_garment_cards()
{
    if (!garment_cards_panel_) {
        return;
    }

    bool has_garment = false;
    for (std::size_t index = 0; index < garment_cards_.size(); ++index) {
        GarmentCard& card = garment_cards_[index];
        const bool is_visible = card.garment_id != 0u && card.is_confirmed;
        card.frame->setVisible(is_visible);
        card.color_button->setEnabled(is_visible);
        has_garment = has_garment || is_visible;
    }

    garment_cards_panel_->setVisible(has_garment);
    garment_cards_panel_->layout()->invalidate();
    garment_cards_panel_->updateGeometry();
}

void MainWindow::clear_garment_card(std::size_t index)
{
    if (index >= garment_cards_.size()) {
        return;
    }

    GarmentCard& card = garment_cards_[index];
    card.garment_id = 0u;
    card.is_confirmed = false;
    card.name_label->clear();
    update_garment_cards();
}

void MainWindow::clear_placement_garment_cards()
{
    for (std::size_t index = 0; index < placement_group_states_.size(); ++index) {
        if (placement_group_states_[index] != PlacementGroupState::Hidden) {
            clear_garment_card(index);
        }
    }
}

void MainWindow::highlight_color_edit_card(std::optional<std::size_t> index)
{
    color_edit_card_index_ = index;
    for (std::size_t card_index = 0; card_index < garment_cards_.size(); ++card_index) {
        const GarmentCard& card = garment_cards_[card_index];
        update_garment_card_color(card_index, card.color);
    }
}

void MainWindow::choose_garment_color(std::size_t index)
{
    if (index >= garment_cards_.size()) {
        return;
    }

    GarmentCard& card = garment_cards_[index];
    if (card.garment_id == 0u || !card.is_confirmed) {
        return;
    }

    const std::uint32_t garment_id = card.garment_id;
    highlight_color_edit_card(index);
    garment_color_panel_->edit_color(card.color, [this, index, garment_id](const glm::vec3& color) {
        simulation_controller_->set_garment_color_by_id(garment_id, color);
        update_garment_card_color(index, color);
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

        const int target_height = container_size.height() / 2;
        const int expanded_max_height = std::min(panel_max_height, available_height);
        const int expanded_height =
            std::clamp(target_height, std::min(panel_min_height, expanded_max_height), expanded_max_height);
        overlay_height = expanded_height;
    }

    browser_panel_->setGeometry(panel_margin, panel_margin, overlay_width, overlay_height);
    browser_panel_->raise();

    const int controls_width = simulation_button_size * simulation_button_count +
                               simulation_button_gap * (simulation_button_count - 1);
    const int controls_x = std::max(panel_margin, (container_size.width() - controls_width) / 2);
    const int controls_y = panel_margin;
    run_button_->setGeometry(controls_x, controls_y, simulation_button_size, simulation_button_size);
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

    const int placement_width = std::min(placement_panel_width, available_width);
    const int placement_x = std::max(panel_margin, container_size.width() - panel_margin - placement_width);
    const int placement_y = controls_y + simulation_button_size + simulation_button_gap;
    int garment_cards_y = placement_y;
    if (has_visible_placement_group()) {
        const int placement_available_height =
            std::max(0, container_size.height() - panel_margin - placement_y);
        const int placement_height =
            std::min(garment_placement_panel_->sizeHint().height(), placement_available_height);
        garment_placement_panel_->setGeometry(placement_x, placement_y, placement_width, placement_height);
        garment_placement_panel_->raise();
        garment_cards_y += placement_height + simulation_button_gap;
    }

    int color_panel_y = garment_cards_y;
    if (garment_cards_panel_->isVisible()) {
        const int cards_available_height =
            std::max(0, container_size.height() - panel_margin - garment_cards_y);
        const int cards_height = std::min(garment_cards_panel_->sizeHint().height(), cards_available_height);
        garment_cards_panel_->setGeometry(placement_x, garment_cards_y, placement_width, cards_height);
        garment_cards_panel_->raise();
        color_panel_y += cards_height + simulation_button_gap;
    }

    if (garment_color_panel_->isVisible()) {
        const int color_available_height =
            std::max(0, container_size.height() - panel_margin - color_panel_y);
        const int color_height = std::min(garment_color_panel_->sizeHint().height(), color_available_height);
        garment_color_panel_->setGeometry(placement_x, color_panel_y, placement_width, color_height);
        garment_color_panel_->raise();
    }
}

// simulation control //
void MainWindow::update_simulation_controls()
{
    if (!simulation_controller_ ||
        !browser_panel_ ||
        !run_button_ ||
        !stop_button_ ||
        !reset_button_ ||
        !garment_placement_panel_ ||
        !garment_color_panel_ ||
        !garment_cards_panel_) {
        return;
    }

    const bool simulation_running = simulation_controller_->is_simulation_running();
    const bool placement_panel_visible = has_visible_placement_group();
    const bool placement_session_active = has_placement_session();
    const bool placement_available =
        placement_panel_visible && !simulation_running && simulation_controller_->is_default_pose();

    run_button_->setIcon(make_simulation_control_icon(
        simulation_running ? SimulationControlIcon::Pause : SimulationControlIcon::Play,
        simulation_running ? QColor{"#f4b400"} : QColor{"#43a047"}));
    run_button_->setToolTip(simulation_running ? "Pause" : "Run");
    run_button_->setEnabled(simulation_running || !placement_session_active);
    stop_button_->setEnabled(simulation_controller_->has_base_positions() && !placement_session_active);
    reset_button_->setEnabled(true);
    browser_panel_->set_motion_selection_enabled(!placement_session_active);
    browser_panel_->set_garment_selection_enabled(simulation_controller_->can_start_garment_placement());
    garment_placement_panel_->setVisible(placement_panel_visible);
    garment_placement_panel_->setEnabled(placement_available);
    update_garment_cards();
    update_placement_actions();
}

// panel update //
void MainWindow::refresh_motion_list()
{
    auto motion_source_paths = asset_io::scan_asset_paths(project_paths_.amass_dir, ".npz");
    motion_source_paths.erase(std::remove_if(motion_source_paths.begin(),
                                             motion_source_paths.end(),
                                             [](const auto& path) { return !is_amass_motion_path(path); }),
                              motion_source_paths.end());

    auto motion_asset_paths = asset_io::scan_asset_paths(project_paths_.motion_asset_dir, ".motion");
    motion_asset_paths.erase(std::remove(motion_asset_paths.begin(),
                                         motion_asset_paths.end(),
                                         project_paths_.default_character_motion_path),
                             motion_asset_paths.end());
    const auto compare_motion_subject = [](const auto& lhs, const auto& rhs) {
        return motion_subject_number(lhs) < motion_subject_number(rhs);
    };
    std::stable_sort(motion_source_paths.begin(), motion_source_paths.end(), compare_motion_subject);
    std::stable_sort(motion_asset_paths.begin(), motion_asset_paths.end(), compare_motion_subject);

    browser_panel_->set_motion_paths(std::move(motion_source_paths), std::move(motion_asset_paths));
}

void MainWindow::refresh_garment_list()
{
    browser_panel_->set_garment_paths(
        asset_io::scan_asset_paths(project_paths_.garment_asset_dir, ".garment"));
}

// converter request //
void MainWindow::request_garment_conversion()
{
    if (garment_converter_->is_running()) {
        QMessageBox::information(this,
                                 "Conversion In Progress",
                                 "변환이 진행 중입니다. 잠시 후 다시 시도해주세요.");
        return;
    }

    const std::optional<ConverterCommand> command = prepare_garment_conversion();

    if (!command) {
        return;
    }

    browser_panel_->set_conversion_active(AssetPanelMode::Garments, true);
    garment_converter_->start_conversion(*command);
}

void MainWindow::request_motion_conversion(const std::filesystem::path& source_path)
{
    if (motion_converter_->is_running()) {
        return;
    }

    const std::filesystem::path motion_asset_path =
        project_paths_.motion_asset_dir / (source_path.stem().string() + ".motion");

    if (std::filesystem::exists(motion_asset_path)) {
        refresh_motion_list();
        return;
    }

    const ConverterCommand command =
        asset_converter_commands::make_motion_command(project_paths_, source_path, motion_asset_path);
    const std::optional<ConverterCommand> valid_command =
        validate_conversion_command(this, AssetPanelMode::Motions, command);
    if (!valid_command) {
        return;
    }

    browser_panel_->set_conversion_active(AssetPanelMode::Motions, true);
    motion_converter_->start_conversion(*valid_command);
}

std::optional<ConverterCommand> MainWindow::prepare_garment_conversion()
{
    const std::filesystem::path default_dir = project_paths_.garment_source_dir;
    const QString selected_file = QFileDialog::getOpenFileName(this,
                                                               "Select Garment OBJ",
                                                               to_q_string(default_dir),
                                                               "Garment OBJ (*.obj)");

    if (selected_file.isEmpty()) {
        return std::nullopt;
    }

    const std::filesystem::path garment_obj_path = selected_file.toStdWString();
    const std::filesystem::path garment_asset_path =
        asset_io::make_garment_asset_path(project_paths_, garment_obj_path);

    if (std::filesystem::exists(garment_asset_path)) {
        return std::nullopt;
    }

    const std::optional<GarmentConversionSettings> settings = select_garment_conversion_settings(this);
    if (!settings) {
        return std::nullopt;
    }

    const auto command = asset_converter_commands::make_garment_command(project_paths_,
                                                                        garment_obj_path,
                                                                        garment_asset_path,
                                                                        settings->attachment_type,
                                                                        settings->garment_category);
    return validate_conversion_command(this, AssetPanelMode::Garments, command);
}
