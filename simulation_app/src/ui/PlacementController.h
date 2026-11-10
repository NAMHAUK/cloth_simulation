#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>

#include <QObject>

class GarmentCardsPanel;
class GarmentColorPanel;
class PlacementPanel;
class SimulationController;
enum GarmentLayer : std::size_t;
struct GarmentMesh;

class PlacementController final : public QObject
{
    Q_OBJECT

public:
    PlacementController(SimulationController& simulation_controller,
                        PlacementPanel& placement_panel,
                        GarmentColorPanel& color_panel,
                        GarmentCardsPanel& cards_panel);
    ~PlacementController();

    PlacementController(const PlacementController&) = delete;
    PlacementController& operator=(const PlacementController&) = delete;

    void load_garment(const std::filesystem::path& asset_path, GarmentMesh mesh);
    void reset();

    bool is_active() const;

Q_SIGNALS:
    void active_changed();
    void layout_changed();

private:
    void connect_placement_panel();
    void connect_color_panels();

    GarmentLayer target_layer() const;
    void end_session();
    void reset_session();

    void set_active(bool active);

    SimulationController& simulation_controller_;
    PlacementPanel& placement_panel_;
    GarmentColorPanel& color_panel_;
    GarmentCardsPanel& cards_panel_;
    std::optional<GarmentLayer> color_edit_layer_;
    bool active_ = false;
};
