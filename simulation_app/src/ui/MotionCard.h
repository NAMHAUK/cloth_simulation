#pragma once

#include <filesystem>

#include <QWidget>

class ElidedLabel;
class QPropertyAnimation;
class QPushButton;

class MotionCard final : public QWidget
{
    Q_OBJECT

public:
    explicit MotionCard(QWidget* parent = nullptr);

    void show_motion(const std::filesystem::path& asset_path,
                     const QString& motion_id,
                     const QString& description);
    void dismiss(const std::filesystem::path& asset_path);

Q_SIGNALS:
    void motion_activated(const std::filesystem::path& asset_path);
    void layout_changed();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    std::filesystem::path asset_path_;
    QPushButton* button_ = nullptr;
    ElidedLabel* name_label_ = nullptr;
    QPropertyAnimation* animation_ = nullptr;
};
