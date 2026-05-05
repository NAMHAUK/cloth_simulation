#pragma once

#include <filesystem>
#include <vector>

#include <QMainWindow>

#include "MotionCache.h"

class MotionBrowserPanel;
class QObject;
class OpenGLViewerWidget;
class QEvent;
class QResizeEvent;
class QWidget;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(const std::filesystem::path& projectRoot, QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void refreshMotionList();
    void loadCachePath(const std::filesystem::path& cachePath);
    void importAmassMotion();
    void updateMotionBrowserGeometry();

    std::filesystem::path projectRoot_;
    std::vector<MotionEntry> motions_;
    QWidget* viewerContainer_ = nullptr;
    MotionBrowserPanel* browserPanel_ = nullptr;
    OpenGLViewerWidget* viewerWidget_ = nullptr;
};
