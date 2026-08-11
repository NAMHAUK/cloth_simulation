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
    connect_placement_panel();
    connect_color_panels();
    set_active(false);
}

PlacementController::~PlacementController() = default;

// Initialization
void PlacementController::connect_placement_panel()
{
    connect(&placement_panel_,
            &PlacementPanel::placement_changed,
            &simulation_controller_,
            &SimulationController::set_garment_placement);

    connect(&placement_panel_,
            &PlacementPanel::color_edit_requested,
            this,
            [this](GarmentLayer layer, const glm::vec3& color) {
                color_edit_layer_ = layer;
                cards_panel_.clear_edit_highlight();
                color_panel_.open_panel(color);
            });

    connect(&placement_panel_, &PlacementPanel::add_upper_requested, this, [this]() {
        placement_panel_.show_upper_section();
        Q_EMIT layout_changed();
    });

    connect(&placement_panel_, &PlacementPanel::remove_upper_requested, this, [this]() {
        if (color_edit_layer_ == GarmentLayer::Upper) {
            color_panel_.close_panel();
        }
        simulation_controller_.remove_garment_placement(GarmentLayer::Upper);
        cards_panel_.clear_card(GarmentLayer::Upper);

        placement_panel_.hide_upper_section();

        Q_EMIT layout_changed();
    });

    connect(&placement_panel_, &PlacementPanel::confirm_requested, this, [this]() {
        color_panel_.close_panel();

        try {
            simulation_controller_.confirm_garment_placement();
        } catch (const std::exception& error) {
            handle_placement_failure(error);
            return;
        }

        cards_panel_.confirm_cards();
        simulation_controller_.start_simulation();
        end_session();
    });

    connect(&placement_panel_, &PlacementPanel::cancel_requested, this, [this]() {
        color_panel_.close_panel();
        simulation_controller_.cancel_garment_placement();
        cards_panel_.clear_unconfirmed_cards();
        end_session();
    });
}

void PlacementController::connect_color_panels()
{
    connect(&color_panel_, &GarmentColorPanel::visibility_changed, this, [this](bool visible) {
        if (!visible) {
            color_edit_layer_.reset();
            cards_panel_.clear_edit_highlight();
        }
        Q_EMIT layout_changed();
    });

    connect(&color_panel_, &GarmentColorPanel::color_changed, this, [this](const glm::vec3& color) {
        if (!color_edit_layer_) {
            return;
        }

        const GarmentLayer layer = *color_edit_layer_;
        simulation_controller_.set_garment_color(layer, color);
        placement_panel_.set_color(layer, color);
        cards_panel_.set_card_color(layer, color);
    });

    connect(&cards_panel_,
            &GarmentCardsPanel::color_edit_requested,
            this,
            [this](GarmentLayer layer, const glm::vec3& color) {
                color_edit_layer_ = layer;
                color_panel_.open_panel(color);
            });
}

// Placement Session
void PlacementController::load_garment(const std::filesystem::path& asset_path, GarmentMesh mesh)
{
    color_panel_.close_panel();

    const GarmentLayer layer = target_layer();

    try {
        simulation_controller_.set_garment_mesh(layer, std::move(mesh));
    } catch (const std::exception& error) {
        handle_placement_failure(error);
        return;
    }

    const QString garment_name = QString::fromStdWString(asset_path.stem().wstring());
    cards_panel_.set_card(layer, garment_name);
    placement_panel_.set_garment(layer, garment_name);

    if (!active_) {
        set_active(true);
        Q_EMIT active_changed();
    }
    Q_EMIT layout_changed();
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

void PlacementController::handle_placement_failure(const std::exception& error)
{
    simulation_controller_.reset_scene();
    reset();
    Q_EMIT active_changed();
    QMessageBox::critical(placement_panel_.window(),
                          "Placement Failed",
                          QStringLiteral("Garment placement failed due to an internal error:\n") +
                              QString::fromUtf8(error.what()));
}

void PlacementController::end_session()
{
    reset_session();
    Q_EMIT active_changed();
    Q_EMIT layout_changed();
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
    Q_EMIT layout_changed();
}

// Accessors
bool PlacementController::is_active() const
{
    return active_;
}
