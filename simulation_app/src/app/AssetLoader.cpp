#include "app/AssetLoader.h"

#include <iostream>
#include <utility>

#include <QtConcurrent/QtConcurrentRun>

AssetLoader::CharacterMeshLoad AssetLoader::load_character_mesh_async(std::filesystem::path motion_asset_path)
{
    CharacterMeshLoad result;
    result.source_path = std::move(motion_asset_path);
    result.is_loaded = ::load_character_mesh(result.source_path, result.mesh);
    return result;
}

AssetLoader::GarmentMeshLoad AssetLoader::load_garment_mesh_async(std::filesystem::path garment_asset_path)
{
    GarmentMeshLoad result;
    result.source_path = std::move(garment_asset_path);
    result.is_loaded = ::load_garment_mesh(result.source_path, result.mesh);
    return result;
}

AssetLoader::AssetLoader(QObject* parent)
    : QObject(parent)
{
    connect(&character_load_watcher_, &QFutureWatcher<CharacterMeshLoad>::finished, this, [this]() {
        finish_character_mesh_load();
    });
    connect(&garment_load_watcher_, &QFutureWatcher<GarmentMeshLoad>::finished, this, [this]() {
        finish_garment_mesh_load();
    });
}

AssetLoader::~AssetLoader()
{
    character_load_watcher_.waitForFinished();
    garment_load_watcher_.waitForFinished();
}

void AssetLoader::set_character_loaded_callback(CharacterLoadedCallback callback)
{
    character_loaded_callback_ = std::move(callback);
}

void AssetLoader::set_character_load_failed_callback(CharacterLoadFailedCallback callback)
{
    character_load_failed_callback_ = std::move(callback);
}

void AssetLoader::set_garment_loaded_callback(GarmentLoadedCallback callback)
{
    garment_loaded_callback_ = std::move(callback);
}

void AssetLoader::set_garment_load_failed_callback(GarmentLoadFailedCallback callback)
{
    garment_load_failed_callback_ = std::move(callback);
}

void AssetLoader::load_character_mesh(std::filesystem::path motion_asset_path)
{
    if (character_load_watcher_.isRunning()) {
        std::cerr << "Character load already in progress.\n";
        return;
    }

    character_load_watcher_.setFuture(QtConcurrent::run(
        &AssetLoader::load_character_mesh_async,
        std::move(motion_asset_path)
    ));
}

void AssetLoader::queue_garment_mesh_load(std::filesystem::path garment_asset_path)
{
    garment_load_queue_.push_back(std::move(garment_asset_path));
    start_next_garment_mesh_load();
}

void AssetLoader::finish_character_mesh_load()
{
    CharacterMeshLoad result = character_load_watcher_.result();
    if (!result.is_loaded) {
        if (character_load_failed_callback_) {
            character_load_failed_callback_(result.source_path);
        }
        return;
    }

    if (character_loaded_callback_) {
        character_loaded_callback_(result.source_path, std::move(result.mesh));
    }
}

void AssetLoader::start_next_garment_mesh_load()
{
    if (garment_load_watcher_.isRunning() || garment_load_queue_.empty()) {
        return;
    }

    std::filesystem::path garment_asset_path = std::move(garment_load_queue_.front());
    garment_load_queue_.pop_front();

    garment_load_watcher_.setFuture(QtConcurrent::run(
        &AssetLoader::load_garment_mesh_async,
        std::move(garment_asset_path)
    ));
}

void AssetLoader::finish_garment_mesh_load()
{
    GarmentMeshLoad result = garment_load_watcher_.result();
    if (!result.is_loaded) {
        if (garment_load_failed_callback_) {
            garment_load_failed_callback_(result.source_path);
        }
    } else if (garment_loaded_callback_) {
        garment_loaded_callback_(std::move(result.mesh));
    }

    start_next_garment_mesh_load();
}
