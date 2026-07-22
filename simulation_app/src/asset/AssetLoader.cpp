#include "asset/AssetLoader.h"

#include "asset/AssetIO.h"

#include <iostream>
#include <utility>

#include <QtConcurrent/QtConcurrentRun>

namespace {
GarmentMeshLoadResult read_garment_mesh(std::filesystem::path garment_asset_path, GarmentRequestId request_id)
{
    GarmentMeshLoadResult result;
    result.source_path = std::move(garment_asset_path);
    result.request_id = request_id;
    result.is_loaded = asset_io::read_garment_mesh(result.source_path, result.mesh);
    return result;
}
}

// 버튼을 통해 캐릭터 or garment load 함수 호출
// 캐릭터  : load_character_mesh -> (background) file read -> call_character_load_callbacks
// garment: load_garment_mesh -> (background) file read -> call_garment_load_callbacks

AssetLoader::AssetLoader(QObject* parent): QObject(parent)
{
    // watcher의 finish 시그널이 오면 호출되는 함수 설정
    connect(&character_load_watcher_, &QFutureWatcher<CharacterMeshLoadResult>::finished, this, [this]() {
        call_character_load_callbacks();
    });
    connect(&garment_load_watcher_, &QFutureWatcher<GarmentMeshLoadResult>::finished, this, [this]() {
        call_garment_load_callbacks();
    });
}

AssetLoader::~AssetLoader()
{
    character_load_watcher_.waitForFinished();
    garment_load_watcher_.waitForFinished();
}

// 캐릭터 //
void AssetLoader::load_character_mesh(std::filesystem::path motion_asset_path)
{
    if (character_load_watcher_.isRunning()) {
        std::cerr << "Character load already in progress.\n";
        return;
    }

    // background에서 character mesh 파일 read
    character_load_watcher_.setFuture(QtConcurrent::run(
        [motion_asset_path = std::move(motion_asset_path)]() mutable {
            CharacterMeshLoadResult result;
            result.source_path = std::move(motion_asset_path);
            result.is_loaded = asset_io::read_character_mesh(result.source_path, result.mesh);
            return result;
        }
    ));
}

void AssetLoader::call_character_load_callbacks()
{
    CharacterMeshLoadResult result = character_load_watcher_.result();
    if (!result.is_loaded) {
        if (character_load_failed_callback_) {
            character_load_failed_callback_(result.source_path);
        }
    } else if (character_loaded_callback_) {
        character_loaded_callback_(result.source_path, std::move(result.mesh));
    }
}

// garment //
// garment load는 queue로 하나씩 처리
GarmentRequestId AssetLoader::load_garment_mesh(std::filesystem::path garment_asset_path)
{
    const GarmentRequestId request_id = ++next_garment_request_id_;
    garment_load_queue_.push_back({std::move(garment_asset_path), request_id});
    load_next_garment_mesh();
    return request_id;
}

void AssetLoader::load_next_garment_mesh()
{
    if (garment_load_watcher_.isRunning() || garment_load_queue_.empty()) {
        return;
    }

    GarmentLoadRequest request = std::move(garment_load_queue_.front());
    garment_load_queue_.pop_front();

    // background에서 garment mesh 파일 read
    garment_load_watcher_.setFuture(QtConcurrent::run(
        read_garment_mesh,
        std::move(request.source_path),
        request.request_id
    ));
}

void AssetLoader::call_garment_load_callbacks()
{
    GarmentMeshLoadResult result = garment_load_watcher_.result();
    if (!result.is_loaded) {
        if (garment_load_failed_callback_) {
            garment_load_failed_callback_(result.request_id, result.source_path);
        }
    } else if (garment_loaded_callback_) {
        garment_loaded_callback_(result.request_id, result.source_path, std::move(result.mesh));
    }

    load_next_garment_mesh();
}

// 콜백 설정 함수 //
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
