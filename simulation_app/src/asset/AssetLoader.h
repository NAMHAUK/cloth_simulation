#pragma once

#include "asset/AssetDataTypes.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>

#include <QFutureWatcher>
#include <QObject>

using GarmentRequestId = std::uint64_t;

struct GarmentMeshLoadResult
{
    std::filesystem::path source_path;
    GarmentRequestId request_id = 0u;
    bool is_loaded = false;
    GarmentMesh mesh;
};

class AssetLoader final : public QObject
{
public:
    using CharacterLoadedCallback = std::function<void(CharacterMesh)>;
    using CharacterLoadFailedCallback = std::function<void(const std::filesystem::path&)>;
    using GarmentLoadedCallback =
        std::function<void(GarmentRequestId, const std::filesystem::path&, GarmentMesh)>;
    using GarmentLoadFailedCallback = std::function<void(GarmentRequestId, const std::filesystem::path&)>;

    explicit AssetLoader(QObject* parent = nullptr);
    ~AssetLoader() override;

    AssetLoader(const AssetLoader&) = delete;
    AssetLoader& operator=(const AssetLoader&) = delete;

    void set_character_loaded_callback(CharacterLoadedCallback callback);
    void set_character_load_failed_callback(CharacterLoadFailedCallback callback);
    void set_garment_loaded_callback(GarmentLoadedCallback callback);
    void set_garment_load_failed_callback(GarmentLoadFailedCallback callback);

    void load_character_mesh(std::filesystem::path motion_asset_path);
    GarmentRequestId load_garment_mesh(std::filesystem::path garment_asset_path);

private:
    struct CharacterMeshLoadResult
    {
        std::filesystem::path source_path;
        bool is_loaded = false;
        CharacterMesh mesh;
    };

    struct GarmentLoadRequest final
    {
        std::filesystem::path source_path;
        GarmentRequestId request_id = 0u;
    };

    void call_character_load_callbacks();
    void load_next_garment_mesh();
    void call_garment_load_callbacks();

    QFutureWatcher<CharacterMeshLoadResult> character_load_watcher_;
    QFutureWatcher<GarmentMeshLoadResult> garment_load_watcher_;
    std::deque<GarmentLoadRequest> garment_load_queue_;
    GarmentRequestId next_garment_request_id_ = 0u;

    CharacterLoadedCallback character_loaded_callback_;
    CharacterLoadFailedCallback character_load_failed_callback_;
    GarmentLoadedCallback garment_loaded_callback_;
    GarmentLoadFailedCallback garment_load_failed_callback_;
};
