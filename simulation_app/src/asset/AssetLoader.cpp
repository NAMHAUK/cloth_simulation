#include "asset/AssetLoader.h"

#include "asset/AssetIO.h"

#include <exception>
#include <iostream>
#include <utility>

#include <QtConcurrent/QtConcurrentRun>

AssetLoader::AssetLoader(QObject* parent) : QObject(parent)
{
    // watcher의 finish 시그널이 오면 호출되는 함수 설정
    connect(&motion_load_watcher_, &QFutureWatcher<MotionLoadResult>::finished, this, [this]() {
        call_motion_load_callbacks();
    });
}

AssetLoader::~AssetLoader()
{
    motion_load_watcher_.waitForFinished();
}

// Motion load //
void AssetLoader::load_motion(std::filesystem::path motion_asset_path)
{
    if (motion_load_watcher_.isRunning()) {
        std::cerr << "Motion load already in progress.\n";
        return;
    }

    // background에서 motion asset read
    motion_load_watcher_.setFuture(
        QtConcurrent::run([motion_asset_path = std::move(motion_asset_path)]() mutable {
            MotionLoadResult result;
            result.source_path = std::move(motion_asset_path);
            try {
                result.motion = asset_io::read_character_motion(result.source_path);
                result.is_loaded = true;
            } catch (const std::exception& error) {
                std::cerr << "Failed to load motion asset: " << result.source_path << '\n';
                std::cerr << "  " << error.what() << '\n';
            }
            return result;
        }));
}

void AssetLoader::call_motion_load_callbacks()
{
    MotionLoadResult result = motion_load_watcher_.result();
    if (!result.is_loaded) {
        if (motion_load_failed_callback_) {
            motion_load_failed_callback_(result.source_path);
        }
    } else if (motion_loaded_callback_) {
        motion_loaded_callback_(std::move(result.motion));
    }
}

// 콜백 설정 함수 //
void AssetLoader::set_motion_loaded_callback(MotionLoadedCallback callback)
{
    motion_loaded_callback_ = std::move(callback);
}

void AssetLoader::set_motion_load_failed_callback(MotionLoadFailedCallback callback)
{
    motion_load_failed_callback_ = std::move(callback);
}
