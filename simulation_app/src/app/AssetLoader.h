#pragma once

#include "io/GarmentAsset.h"
#include "io/MotionAsset.h"

#include <deque>
#include <filesystem>
#include <functional>

#include <QFutureWatcher>
#include <QObject>

class AssetLoader final : public QObject {
public:
    using CharacterLoadedCallback = std::function<void(const std::filesystem::path&, CharacterMesh)>;
    using CharacterLoadFailedCallback = std::function<void(const std::filesystem::path&)>;
    using GarmentLoadedCallback = std::function<void(GarmentMesh)>;
    using GarmentLoadFailedCallback = std::function<void(const std::filesystem::path&)>;

    explicit AssetLoader(QObject* parent = nullptr);
    ~AssetLoader() override;

    AssetLoader(const AssetLoader&) = delete;
    AssetLoader& operator=(const AssetLoader&) = delete;

    void set_character_loaded_callback(CharacterLoadedCallback callback);
    void set_character_load_failed_callback(CharacterLoadFailedCallback callback);
    void set_garment_loaded_callback(GarmentLoadedCallback callback);
    void set_garment_load_failed_callback(GarmentLoadFailedCallback callback);

    void load_character_mesh(std::filesystem::path motion_asset_path);
    void queue_garment_mesh_load(std::filesystem::path garment_asset_path);

private:
    struct CharacterMeshLoad {
        std::filesystem::path source_path;
        bool is_loaded = false;
        CharacterMesh mesh;
    };

    struct GarmentMeshLoad {
        std::filesystem::path source_path;
        bool is_loaded = false;
        GarmentMesh mesh;
    };

    static CharacterMeshLoad load_character_mesh_async(std::filesystem::path motion_asset_path);
    static GarmentMeshLoad load_garment_mesh_async(std::filesystem::path garment_asset_path);

    void finish_character_mesh_load();
    void start_next_garment_mesh_load();
    void finish_garment_mesh_load();

    QFutureWatcher<CharacterMeshLoad> character_load_watcher_;
    QFutureWatcher<GarmentMeshLoad> garment_load_watcher_;
    std::deque<std::filesystem::path> garment_load_queue_;

    CharacterLoadedCallback character_loaded_callback_;
    CharacterLoadFailedCallback character_load_failed_callback_;
    GarmentLoadedCallback garment_loaded_callback_;
    GarmentLoadFailedCallback garment_load_failed_callback_;
};
