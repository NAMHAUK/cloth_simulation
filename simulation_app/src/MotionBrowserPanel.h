#pragma once

#include "MotionCache.h"

#include <functional>
#include <filesystem>
#include <vector>

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class QWidget;

class MotionBrowserPanel final : public QWidget {
public:
    explicit MotionBrowserPanel(QWidget* parent = nullptr);

    void setMotions(std::vector<MotionEntry> motions);
    void setCurrentCache(const std::filesystem::path& cachePath);
    void setMotionSelectedCallback(std::function<void(const std::filesystem::path&)> callback);
    void setImportRequestedCallback(std::function<void()> callback);
    void setExpansionChangedCallback(std::function<void()> callback);

    bool isExpanded() const;

private:
    void rebuildList();
    void setExpanded(bool expanded);
    void updateExpandedState();

    QPushButton* toggleButton_ = nullptr;
    QWidget* expandedPanel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QListWidget* listWidget_ = nullptr;
    QPushButton* importButton_ = nullptr;
    bool expanded_ = false;
    std::vector<MotionEntry> motions_;
    std::function<void(const std::filesystem::path&)> motionSelectedCallback_;
    std::function<void()> importRequestedCallback_;
    std::function<void()> expansionChangedCallback_;
};
