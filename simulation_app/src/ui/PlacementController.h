#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>

class GarmentCardsPanel;
class GarmentColorPanel;
class PlacementPanel;
class SimulationController;
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
    void end_session();
    void reset();

    bool is_active() const;

    void set_active_changed_callback(ActiveChangedCallback callback);
    void set_layout_changed_callback(LayoutChangedCallback callback);

private:
    enum class PlacementPhase
    {
        Hidden,
        Waiting,
        Ready,
    };

    void setup_placement_callbacks();
    void setup_color_callbacks();

    void reset_session();

    void update_controls();
    void update_button_state();

    void notify_active_changed();
    void notify_layout_changed();

    SimulationController& simulation_controller_;
    PlacementPanel& placement_panel_;
    GarmentColorPanel& color_panel_;
    GarmentCardsPanel& cards_panel_;
    std::array<PlacementPhase, 2> placement_phases_{};
    bool is_upper_color_editing_ = false;
    bool active_ = false;
    ActiveChangedCallback active_changed_callback_;
    LayoutChangedCallback layout_changed_callback_;
};
