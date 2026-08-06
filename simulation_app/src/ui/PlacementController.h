#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>

class GarmentCardsPanel;
class GarmentColorPanel;
class PlacementPanel;
class SimulationController;
enum GarmentLayer : std::size_t;
struct GarmentMesh;

class PlacementController final
{
public:
    using ActiveChangedCallback = std::function<void()>;
    using LayoutChangedCallback = std::function<void()>;

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

    void set_active_changed_callback(ActiveChangedCallback callback);
    void set_layout_changed_callback(LayoutChangedCallback callback);

private:
    void setup_placement_callbacks();
    void setup_color_callbacks();

    GarmentLayer target_layer() const;
    void end_session();
    void reset_session();

    void set_active(bool active);

    void notify_active_changed();
    void notify_layout_changed();

    SimulationController& simulation_controller_;
    PlacementPanel& placement_panel_;
    GarmentColorPanel& color_panel_;
    GarmentCardsPanel& cards_panel_;
    std::optional<GarmentLayer> color_edit_layer_;
    bool active_ = false;
    ActiveChangedCallback active_changed_callback_;
    LayoutChangedCallback layout_changed_callback_;
};
