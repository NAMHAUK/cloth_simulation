#pragma once

#include "asset/AssetDataTypes.h"

#include <filesystem>
#include <functional>

#include <QFutureWatcher>
#include <QObject>

class AssetLoader final : public QObject
{
public:
    using CharacterLoadedCallback = std::function<void(CharacterMesh)>;
    using CharacterLoadFailedCallback = std::function<void(const std::filesystem::path&)>;

    explicit AssetLoader(QObject* parent = nullptr);
    ~AssetLoader() override;

    AssetLoader(const AssetLoader&) = delete;
    AssetLoader& operator=(const AssetLoader&) = delete;

    void set_character_loaded_callback(CharacterLoadedCallback callback);
    void set_character_load_failed_callback(CharacterLoadFailedCallback callback);

    void load_character_mesh(std::filesystem::path motion_asset_path);

private:
    struct CharacterMeshLoadResult
    {
        std::filesystem::path source_path;
        bool is_loaded = false;
        CharacterMesh mesh;
    };

    void call_character_load_callbacks();

    QFutureWatcher<CharacterMeshLoadResult> character_load_watcher_;

    CharacterLoadedCallback character_loaded_callback_;
    CharacterLoadFailedCallback character_load_failed_callback_;
};
