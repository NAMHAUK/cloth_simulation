#include "asset/AssetLoader.h"

#include "asset/AssetIO.h"

#include <iostream>
#include <utility>

#include <QtConcurrent/QtConcurrentRun>

AssetLoader::AssetLoader(QObject* parent) : QObject(parent)
{
    // watcher의 finish 시그널이 오면 호출되는 함수 설정
    connect(&character_load_watcher_, &QFutureWatcher<CharacterMeshLoadResult>::finished, this, [this]() {
        call_character_load_callbacks();
    });
}

AssetLoader::~AssetLoader()
{
    character_load_watcher_.waitForFinished();
}

// 캐릭터 //
void AssetLoader::load_character_mesh(std::filesystem::path motion_asset_path)
{
    if (character_load_watcher_.isRunning()) {
        std::cerr << "Character load already in progress.\n";
        return;
    }

    // background에서 character mesh 파일 read
    character_load_watcher_.setFuture(
        QtConcurrent::run([motion_asset_path = std::move(motion_asset_path)]() mutable {
            CharacterMeshLoadResult result;
            result.source_path = std::move(motion_asset_path);
            result.is_loaded = asset_io::read_character_mesh(result.source_path, result.mesh);
            return result;
        }));
}

void AssetLoader::call_character_load_callbacks()
{
    CharacterMeshLoadResult result = character_load_watcher_.result();
    if (!result.is_loaded) {
        if (character_load_failed_callback_) {
            character_load_failed_callback_(result.source_path);
        }
    } else if (character_loaded_callback_) {
        character_loaded_callback_(std::move(result.mesh));
    }
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
