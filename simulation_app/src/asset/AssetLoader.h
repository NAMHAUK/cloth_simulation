#pragma once

#include "asset/AssetDataTypes.h"

#include <filesystem>
#include <functional>

#include <QFutureWatcher>
#include <QObject>

class AssetLoader final : public QObject
{
public:
    using MotionLoadedCallback = std::function<void(CharacterMotion)>;
    using MotionLoadFailedCallback = std::function<void(const std::filesystem::path&)>;

    explicit AssetLoader(QObject* parent = nullptr);
    ~AssetLoader() override;

    AssetLoader(const AssetLoader&) = delete;
    AssetLoader& operator=(const AssetLoader&) = delete;

    void set_motion_loaded_callback(MotionLoadedCallback callback);
    void set_motion_load_failed_callback(MotionLoadFailedCallback callback);

    void load_motion(std::filesystem::path motion_asset_path);

private:
    struct MotionLoadResult
    {
        std::filesystem::path source_path;
        bool is_loaded = false;
        CharacterMotion motion;
    };

    void call_motion_load_callbacks();

    QFutureWatcher<MotionLoadResult> motion_load_watcher_;

    MotionLoadedCallback motion_loaded_callback_;
    MotionLoadFailedCallback motion_load_failed_callback_;
};
