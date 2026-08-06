#include "ui/PlacementController.h"

#include "asset/AssetDataTypes.h"
#include "simulation/SimulationController.h"
#include "ui/GarmentCardsPanel.h"
#include "ui/GarmentColorPanel.h"
#include "ui/PlacementPanel.h"

#include <algorithm>
#include <utility>

#include <QMessageBox>
#include <QString>

PlacementController::PlacementController(SimulationController& simulation_controller,
                                         PlacementPanel& placement_panel,
                                         GarmentColorPanel& color_panel,
                                         GarmentCardsPanel& cards_panel)
    : simulation_controller_(simulation_controller),
      placement_panel_(placement_panel),
      color_panel_(color_panel),
      cards_panel_(cards_panel)
{
    setup_placement_callbacks();
    setup_color_callbacks();
    update_controls();
}

PlacementController::~PlacementController()
{
    color_panel_.set_visibility_changed_callback({});
    color_panel_.close_panel();
    cards_panel_.set_color_edit_callback({});
    placement_panel_.set_placement_changed_callback({});
    placement_panel_.set_color_changed_callback({});
    placement_panel_.set_open_color_editor_callback({});
    placement_panel_.set_add_upper_placement_callback({});
    placement_panel_.set_remove_upper_placement_callback({});
    placement_panel_.set_confirm_callback({});
    placement_panel_.set_cancel_callback({});
}

// Initialization
void PlacementController::setup_placement_callbacks()
{
    placement_panel_.set_placement_changed_callback(
        [this](GarmentLayer layer, const glm::vec3& offset, float scale) {
            simulation_controller_.set_garment_placement(layer, offset, scale);
        });

    placement_panel_.set_color_changed_callback([this](GarmentLayer layer, const glm::vec3& color) {
        simulation_controller_.set_garment_color(layer, color);
        cards_panel_.set_card_color(layer, color);
    });

    placement_panel_.set_open_color_editor_callback(
        [this](GarmentLayer layer, const glm::vec3& color, PlacementPanel::ColorSelectedCallback callback) {
            is_upper_color_editing_ = layer == GarmentLayer::Upper;
            cards_panel_.clear_edit_highlight();
            color_panel_.edit_color(color, std::move(callback));
        });

    placement_panel_.set_add_upper_placement_callback([this]() {
        placement_phases_[GarmentLayer::Upper] = PlacementPhase::Waiting;
        placement_panel_.show_upper_section();
        notify_layout_changed();
    });

    placement_panel_.set_remove_upper_placement_callback([this]() {
        if (is_upper_color_editing_) {
            color_panel_.close_panel();
        }
        simulation_controller_.remove_garment_placement(GarmentLayer::Upper);
        cards_panel_.clear_card(GarmentLayer::Upper);

        placement_phases_[GarmentLayer::Upper] = PlacementPhase::Hidden;
        placement_panel_.hide_upper_section();

        update_controls();
        notify_layout_changed();
    });

    placement_panel_.set_confirm_callback([this]() {
        color_panel_.close_panel();

        if (!simulation_controller_.confirm_garment_placement()) {
            QMessageBox::warning(placement_panel_.window(),
                                 "Placement Failed",
                                 "Failed to initialize garment placement.");
            return;
        }

        for (GarmentLayer layer : {GarmentLayer::Lower, GarmentLayer::Upper}) {
            if (placement_phases_[layer] == PlacementPhase::Ready) {
                cards_panel_.confirm_card(layer);
            }
        }
        simulation_controller_.start_simulation();
        end_session();
    });

    placement_panel_.set_cancel_callback([this]() {
        color_panel_.close_panel();
        simulation_controller_.cancel_garment_placement();
        clear_placement_cards();
        end_session();
    });
}

void PlacementController::setup_color_callbacks()
{
    color_panel_.set_visibility_changed_callback([this]() {
        if (!color_panel_.isVisible()) {
            is_upper_color_editing_ = false;
            cards_panel_.clear_edit_highlight();
        }
        notify_layout_changed();
    });

    cards_panel_.set_color_edit_callback([this](GarmentLayer layer, const glm::vec3& color) {
        is_upper_color_editing_ = (layer == GarmentLayer::Upper);
        color_panel_.edit_color(color, [this, layer](const glm::vec3& selected_color) {
            simulation_controller_.set_garment_color(layer, selected_color);
            cards_panel_.set_card_color(layer, selected_color);
        });
    });
}

// Placement Session
void PlacementController::load_garment(const std::filesystem::path& asset_path, GarmentMesh mesh)
{
    color_panel_.close_panel();
    const GarmentLayer layer = active_ ? placement_panel_.active_layer()
                               : simulation_controller_.garment_count() == 0u ? GarmentLayer::Lower
                                                                              : GarmentLayer::Upper;

    if (!simulation_controller_.set_garment_mesh(layer, std::move(mesh))) {
        QMessageBox::warning(placement_panel_.window(),
                             "Load Failed",
                             "Failed to apply garment:\n" + QString::fromStdWString(asset_path.wstring()));
        return;
    }

    if (!active_) {
        active_ = true;
        notify_active_changed();
    }
    placement_phases_[layer] = PlacementPhase::Ready;
    const QString garment_name = QString::fromStdWString(asset_path.stem().wstring());
    const glm::vec3 color = simulation_controller_.garment_placement_color(layer);
    cards_panel_.set_card(layer, garment_name, color);
    placement_panel_.set_garment(layer, garment_name, color);
    update_controls();
    notify_layout_changed();
}

void PlacementController::end_session()
{
    if (!active_) {
        return;
    }

    reset_session();
    update_controls();
    notify_active_changed();
    notify_layout_changed();
}

void PlacementController::reset()
{
    color_panel_.close_panel();
    cards_panel_.clear_cards();
    reset_session();
    update_controls();
    notify_layout_changed();
}

// Placement State
void PlacementController::reset_session()
{
    active_ = false;
    placement_phases_.fill(PlacementPhase::Hidden);
    placement_panel_.reset();
}

void PlacementController::clear_placement_cards()
{
    for (std::size_t index = 0; index < placement_phases_.size(); ++index) {
        if (placement_phases_[index] != PlacementPhase::Hidden) {
            cards_panel_.clear_card(static_cast<GarmentLayer>(index));
        }
    }
}

// UI Updates
void PlacementController::update_controls()
{
    const bool simulation_running = simulation_controller_.is_simulation_running();
    const bool placement_available =
        active_ && !simulation_running && simulation_controller_.is_default_pose();

    placement_panel_.setVisible(active_);
    placement_panel_.setEnabled(placement_available);
    update_button_state();
}

void PlacementController::update_button_state()
{
    const bool all_visible_groups_ready =
        std::none_of(placement_phases_.begin(), placement_phases_.end(), [](PlacementPhase phase) {
            return phase == PlacementPhase::Waiting;
        });

    placement_panel_.set_confirm_button_enabled(active_ && all_visible_groups_ready);
    placement_panel_.set_add_button_enabled(placement_phases_[GarmentLayer::Lower] == PlacementPhase::Ready &&
                                            placement_phases_[GarmentLayer::Upper] ==
                                                PlacementPhase::Hidden &&
                                            simulation_controller_.garment_count() < 2u);
}

// Accessors
bool PlacementController::is_active() const
{
    return active_;
}

// Callback Registration
void PlacementController::set_active_changed_callback(ActiveChangedCallback callback)
{
    active_changed_callback_ = std::move(callback);
}

void PlacementController::set_layout_changed_callback(LayoutChangedCallback callback)
{
    layout_changed_callback_ = std::move(callback);
}

// Callback Notification
void PlacementController::notify_active_changed()
{
    if (active_changed_callback_) {
        active_changed_callback_();
    }
}

void PlacementController::notify_layout_changed()
{
    if (layout_changed_callback_) {
        layout_changed_callback_();
    }
}
