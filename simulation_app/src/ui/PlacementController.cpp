#include "ui/PlacementController.h"

#include "asset/AssetDataTypes.h"
#include "simulation/SimulationController.h"
#include "ui/GarmentCardsPanel.h"
#include "ui/GarmentColorPanel.h"
#include "ui/PlacementPanel.h"

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
    set_active(false);
}

PlacementController::~PlacementController()
{
    color_panel_.set_visibility_changed_callback({});
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
            color_edit_layer_ = layer;
            cards_panel_.clear_edit_highlight();
            color_panel_.open_panel(color, std::move(callback));
        });

    placement_panel_.set_add_upper_placement_callback([this]() {
        placement_panel_.show_upper_section();
        notify_layout_changed();
    });

    placement_panel_.set_remove_upper_placement_callback([this]() {
        if (color_edit_layer_ == GarmentLayer::Upper) {
            color_panel_.close_panel();
        }
        simulation_controller_.remove_garment_placement(GarmentLayer::Upper);
        cards_panel_.clear_card(GarmentLayer::Upper);

        placement_panel_.hide_upper_section();

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

        cards_panel_.confirm_cards();
        simulation_controller_.start_simulation();
        end_session();
    });

    placement_panel_.set_cancel_callback([this]() {
        color_panel_.close_panel();
        simulation_controller_.cancel_garment_placement();
        cards_panel_.clear_unconfirmed_cards();
        end_session();
    });
}

void PlacementController::setup_color_callbacks()
{
    color_panel_.set_visibility_changed_callback([this](bool visible) {
        if (!visible) {
            color_edit_layer_.reset();
            cards_panel_.clear_edit_highlight();
        }
        notify_layout_changed();
    });

    cards_panel_.set_color_edit_callback([this](GarmentLayer layer, const glm::vec3& color) {
        color_edit_layer_ = layer;

        color_panel_.open_panel(color, [this, layer](const glm::vec3& selected_color) {
            simulation_controller_.set_garment_color(layer, selected_color);
            cards_panel_.set_card_color(layer, selected_color);
        });
    });
}

// Placement Session
void PlacementController::load_garment(const std::filesystem::path& asset_path, GarmentMesh mesh)
{
    color_panel_.close_panel();

    const GarmentLayer layer = target_layer();

    if (!simulation_controller_.set_garment_mesh(layer, std::move(mesh))) {
        QMessageBox::warning(placement_panel_.window(),
                             "Load Failed",
                             "Failed to apply garment:\n" + QString::fromStdWString(asset_path.wstring()));
        return;
    }

    const QString garment_name = QString::fromStdWString(asset_path.stem().wstring());
    const glm::vec3 color = simulation_controller_.garment_placement_color(layer);
    cards_panel_.set_card(layer, garment_name);
    placement_panel_.set_garment(layer, garment_name, color);

    if (!active_) {
        set_active(true);
        notify_active_changed();
    }
    notify_layout_changed();
}

GarmentLayer PlacementController::target_layer() const
{
    if (active_) {
        return placement_panel_.active_layer();
    }
    if (simulation_controller_.garment_count() == 0u) {
        return GarmentLayer::Lower;
    }
    return GarmentLayer::Upper;
}

void PlacementController::end_session()
{
    reset_session();
    notify_active_changed();
    notify_layout_changed();
}

void PlacementController::reset_session()
{
    set_active(false);
    placement_panel_.reset();
}

void PlacementController::set_active(bool active)
{
    active_ = active;
    placement_panel_.setVisible(active);
    placement_panel_.setEnabled(active);
}

void PlacementController::reset()
{
    color_panel_.close_panel();
    cards_panel_.clear_cards();
    reset_session();
    notify_layout_changed();
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
    active_changed_callback_();
}

void PlacementController::notify_layout_changed()
{
    layout_changed_callback_();
}
