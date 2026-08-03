#include "ui/AssetBrowserPanel.h"

#include "asset/AssetConverter.h"
#include "asset/AssetConverterCommands.h"
#include "asset/AssetIO.h"
#include "asset/AssetLoader.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>

#include <QBitmap>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QRegion>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QSize>
#include <QSizePolicy>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <iostream>
#include <utility>

class ElidedLabel final : public QLabel
{
public:
    explicit ElidedLabel(const QString& text, QWidget* parent = nullptr) : QLabel(parent)
    {
        set_full_text(text);
    }

    void set_full_text(const QString& text)
    {
        full_text_ = text;
        setToolTip(full_text_);
        update_text();
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        update_text();
    }

private:
    void update_text() { QLabel::setText(fontMetrics().elidedText(full_text_, Qt::ElideRight, width())); }

    QString full_text_;
};

namespace {
constexpr int close_icon_size = 12;
constexpr int action_icon_size = 20;

int motion_subject_number(const std::filesystem::path& motion_path)
{
    bool is_number = false;
    const int subject_number =
        QString::fromStdWString(motion_path.stem().wstring()).section('_', 0, 0).toInt(&is_number);
    return is_number ? subject_number : std::numeric_limits<int>::max();
}

bool is_amass_motion_path(const std::filesystem::path& motion_path)
{
    const QString motion_name = QString::fromStdWString(motion_path.stem().wstring());
    return motion_name.size() > 6 && motion_name.endsWith("_poses");
}

void show_conversion_failure(QWidget* parent, AssetPanelMode mode)
{
    const char* message = mode == AssetPanelMode::Motions ? "Failed to convert AMASS motion."
                                                          : "Failed to convert garment OBJ.";

    QMessageBox::warning(parent, "Conversion Failed", message);
}

struct GarmentConversionSettings final
{
    QString attachment_type;
    QString garment_category;
};

std::optional<GarmentConversionSettings> select_garment_conversion_settings(QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle("Garment Conversion Settings");

    QComboBox attachment_type;
    attachment_type.addItem("None", "none");
    attachment_type.addItem("Waistband", "waistband");

    QComboBox garment_category;
    garment_category.addItem("Top", "top");
    garment_category.addItem("Bottom", "bottom");
    garment_category.addItem("Full-Body", "full-body");

    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QFormLayout layout(&dialog);
    layout.addRow("Attachment Type", &attachment_type);
    layout.addRow("Garment Category", &garment_category);
    layout.addRow(&buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    return GarmentConversionSettings{attachment_type.currentData().toString(),
                                     garment_category.currentData().toString()};
}

QString make_asset_display_name(const std::filesystem::path& asset_path)
{
    const std::filesystem::path stem = asset_path.stem();
    const std::filesystem::path display_path = stem.empty() ? asset_path.filename() : stem;
    return QString::fromStdWString(display_path.wstring());
}

QString make_motion_id(const std::filesystem::path& motion_path)
{
    QString motion_id = make_asset_display_name(motion_path);
    const QString pose_suffix = "_poses";
    if (motion_id.endsWith(pose_suffix)) {
        motion_id.chop(pose_suffix.size());
    }
    return motion_id;
}

QString make_subject_id(const std::filesystem::path& motion_path)
{
    return make_motion_id(motion_path).section('_', 0, 0);
}

QHash<QString, QString> read_descriptions(const std::filesystem::path& catalog_path)
{
    QFile catalog_file(QString::fromStdWString(catalog_path.wstring()));
    if (!catalog_file.open(QIODevice::ReadOnly)) {
        std::cerr << "Failed to open description catalog: " << catalog_path << '\n';
        return {};
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(catalog_file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        std::cerr << "Failed to parse description catalog: " << catalog_path << '\n';
        return {};
    }

    QHash<QString, QString> descriptions;
    const QJsonObject catalog = document.object();
    for (auto item = catalog.constBegin(); item != catalog.constEnd(); ++item) {
        descriptions.insert(item.key(), item.value().toString());
    }
    return descriptions;
}

class LoadingSpinner final : public QWidget
{
public:
    explicit LoadingSpinner(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedSize(18, 18);
        setStyleSheet("background: transparent; border: none;");
        connect(&timer_, &QTimer::timeout, this, [this]() {
            phase_ = (phase_ + 1) % segment_count;
            update();
        });
        timer_.start(80);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(width() * 0.5, height() * 0.5);
        painter.rotate(phase_ * segment_angle);

        for (int segment = 0; segment < segment_count; ++segment) {
            QColor color{"#3a3a3a"};
            color.setAlphaF(static_cast<float>(segment + 1) / segment_count);
            painter.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(0.0, -7.0), QPointF(0.0, -4.0));
            painter.rotate(segment_angle);
        }
    }

private:
    static constexpr int segment_count = 8;
    static constexpr int segment_angle = 360 / segment_count;
    QTimer timer_;
    int phase_ = 0;
};

QWidget* make_centered_cell_widget(QWidget* content)
{
    auto* container = new QWidget();
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch(1);
    layout->addWidget(content);
    layout->addStretch(1);
    return container;
}

QIcon make_white_icon(const std::filesystem::path& icon_path)
{
    QPixmap icon(QString::fromStdWString(icon_path.wstring()));
    icon = icon.copy(QRegion(icon.mask()).boundingRect());

    QPainter painter(&icon);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(icon.rect(), Qt::white);
    painter.end();
    return QIcon(icon);
}

QIcon make_close_icon()
{
    QPixmap pixmap(close_icon_size, close_icon_size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen{Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap});
    painter.drawLine(2, 2, close_icon_size - 2, close_icon_size - 2);
    painter.drawLine(close_icon_size - 2, 2, 2, close_icon_size - 2);
    return QIcon{pixmap};
}

QIcon make_plus_icon()
{
    QPixmap pixmap(action_icon_size, action_icon_size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen{Qt::white, 3.0, Qt::SolidLine, Qt::RoundCap});
    painter.drawLine(4, action_icon_size / 2, action_icon_size - 4, action_icon_size / 2);
    painter.drawLine(action_icon_size / 2, 4, action_icon_size / 2, action_icon_size - 4);
    return QIcon{pixmap};
}
}

AssetBrowserPanel::AssetBrowserPanel(const ProjectPaths& project_paths, QWidget* parent)
    : QWidget(parent),
      project_paths_(project_paths),
      motion_descriptions_(read_descriptions(project_paths_.motion_asset_dir / "motion_catalog.json")),
      subject_descriptions_(read_descriptions(project_paths_.motion_asset_dir / "subject_catalog.json"))
{
    asset_loader_ = new AssetLoader(this);
    motion_converter_ = new AssetConverter(this);
    garment_converter_ = new AssetConverter(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto* button_layout = new QHBoxLayout();
    button_layout->setContentsMargins(0, 0, 0, 0);
    button_layout->setSpacing(6);

    const std::filesystem::path icon_dir =
        std::filesystem::path(PROJECT_ROOT_DIR) / "data" / "sources" / "icon";

    toggle_button_ = new QPushButton(this);
    toggle_button_->setIcon(make_white_icon(icon_dir / "motion.png"));
    toggle_button_->setIconSize(QSize(76, 76));
    toggle_button_->setToolTip("Motion");
    toggle_button_->setCheckable(true);
    toggle_button_->setFixedSize(86, 86);
    toggle_button_->setStyleSheet("QPushButton {"
                                  "  background-color: #3a3a3a;"
                                  "  color: white;"
                                  "  border: 1px solid #242424;"
                                  "  border-radius: 14px;"
                                  "  padding: 4px;"
                                  "  font-weight: 600;"
                                  "}"
                                  "QPushButton:checked {"
                                  "  background-color: #2f2f2f;"
                                  "}"
                                  "QPushButton:hover {"
                                  "  background-color: #4a4a4a;"
                                  "}"
                                  "QPushButton:disabled {"
                                  "  background-color: #555555;"
                                  "  color: #bdbdbd;"
                                  "  border-color: #444444;"
                                  "}");

    garment_button_ = new QPushButton(this);
    garment_button_->setIcon(make_white_icon(icon_dir / "garment.png"));
    garment_button_->setIconSize(QSize(76, 76));
    garment_button_->setToolTip("Garment");
    garment_button_->setCheckable(true);
    garment_button_->setFixedSize(86, 86);
    garment_button_->setStyleSheet(toggle_button_->styleSheet());

    expanded_panel_ = new QWidget(this);
    expanded_panel_->setObjectName("assetBrowserExpandedPanel");
    expanded_panel_->setStyleSheet("#assetBrowserExpandedPanel {"
                                   "  background-color: rgba(245, 245, 245, 235);"
                                   "  border: 1px solid #9a9a9a;"
                                   "  border-radius: 4px;"
                                   "}"
                                   "QTableWidget {"
                                   "  background-color: white;"
                                   "  border: 1px solid #b5b5b5;"
                                   "  gridline-color: #b5b5b5;"
                                   "}"
                                   "QHeaderView::section {"
                                   "  background-color: #5a5a5a;"
                                   "  color: white;"
                                   "  border: none;"
                                   "  border-right: 1px solid #b5b5b5;"
                                   "  border-bottom: 1px solid #b5b5b5;"
                                   "  padding: 4px;"
                                   "  font-weight: 600;"
                                   "}"
                                   "QHeaderView::section:last {"
                                   "  border-right: none;"
                                   "}"
                                   "QPushButton {"
                                   "  background-color: #5a5a5a;"
                                   "  color: white;"
                                   "  border: 1px solid #242424;"
                                   "  border-radius: 4px;"
                                   "}"
                                   "QPushButton:hover {"
                                   "  background-color: #7a7a7a;"
                                   "}");

    auto* panel_layout = new QVBoxLayout(expanded_panel_);
    panel_layout->setContentsMargins(10, 10, 10, 10);
    panel_layout->setSpacing(8);

    auto* title_layout = new QHBoxLayout();
    title_layout->setContentsMargins(0, 0, 0, 0);
    title_layout->setSpacing(6);

    subject_back_button_ = new QPushButton(QString("%1 Subjects /").arg(QChar(0x2039)), expanded_panel_);
    subject_back_button_->setCursor(Qt::PointingHandCursor);
    subject_back_button_->setMinimumHeight(26);
    subject_back_button_->setStyleSheet("QPushButton {"
                                        "  background: transparent;"
                                        "  border: none;"
                                        "  color: #777777;"
                                        "  font-size: 12px;"
                                        "  padding: 0;"
                                        "}"
                                        "QPushButton:hover {"
                                        "  color: #1f6feb;"
                                        "}");
    subject_back_button_->setToolTip("Back to subjects list");

    auto* close_button = new QPushButton(expanded_panel_);
    close_button->setFixedSize(26, 26);
    close_button->setIcon(make_close_icon());
    close_button->setIconSize(QSize{close_icon_size, close_icon_size});
    close_button->setStyleSheet("padding: 0;");
    close_button->setToolTip("cancel");

    title_label_ = new ElidedLabel("Available motions (0)", expanded_panel_);
    title_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    title_label_->setStyleSheet("font-size: 13px; font-weight: 600;");

    table_widget_ = new QTableWidget(expanded_panel_);
    table_widget_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_widget_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_widget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_widget_->setTextElideMode(Qt::ElideRight);
    table_widget_->setShowGrid(true);
    table_widget_->verticalHeader()->setVisible(false);

    import_button_ = new QPushButton(expanded_panel_);
    import_button_->setMinimumHeight(32);
    import_button_->setIcon(make_white_icon(icon_dir / "file-import-solid-full.svg"));
    import_button_->setIconSize(QSize{action_icon_size, action_icon_size});
    import_button_->setStyleSheet("QPushButton {"
                                  "  background-color: #1f6feb;"
                                  "  color: white;"
                                  "  border: 1px solid #1158c7;"
                                  "  border-radius: 4px;"
                                  "  padding: 0;"
                                  "}"
                                  "QPushButton:hover {"
                                  "  background-color: #2f81f7;"
                                  "}"
                                  "QPushButton:disabled {"
                                  "  background-color: #8caee6;"
                                  "  color: #dddddd;"
                                  "}");
    import_button_->setToolTip("Import garment");

    button_layout->addWidget(toggle_button_);
    button_layout->addWidget(garment_button_);
    button_layout->addStretch(1);

    layout->addLayout(button_layout);
    layout->addWidget(expanded_panel_, 1);

    title_layout->addWidget(subject_back_button_);
    title_layout->addWidget(title_label_, 1);
    title_layout->addWidget(close_button);
    panel_layout->addLayout(title_layout);
    panel_layout->addWidget(table_widget_, 1);
    panel_layout->addWidget(import_button_);

    connect(toggle_button_, &QPushButton::clicked, this, [this]() {
        if (expanded_ && panel_mode_ == AssetPanelMode::Motions) {
            set_expanded(false);
            return;
        }
        open_list(AssetPanelMode::Motions);
    });

    connect(garment_button_, &QPushButton::clicked, this, [this]() {
        if (expanded_ && panel_mode_ == AssetPanelMode::Garments) {
            set_expanded(false);
            return;
        }
        open_list(AssetPanelMode::Garments);
    });

    connect(subject_back_button_, &QPushButton::clicked, this, [this]() {
        const QString subject_id = selected_subject_id_;
        selected_subject_id_.clear();
        rebuild_list();
        for (int row = 0; row < table_widget_->rowCount(); ++row) {
            QTableWidgetItem* item = table_widget_->item(row, 0);
            if (item && item->data(Qt::UserRole).toString() == subject_id) {
                table_widget_->setCurrentItem(item);
                table_widget_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
                break;
            }
        }
    });

    connect(close_button, &QPushButton::clicked, this, [this]() { set_expanded(false); });

    connect(table_widget_, &QTableWidget::cellClicked, this, [this](int row) { select_table_row(row); });

    connect(import_button_, &QPushButton::clicked, this, [this]() { request_garment_conversion(); });

    asset_loader_->set_motion_loaded_callback([this](CharacterMesh mesh) {
        if (motion_loaded_callback_) {
            motion_loaded_callback_(std::move(mesh));
        }
        if (motion_loading_changed_callback_) {
            motion_loading_changed_callback_(false);
        }
    });
    asset_loader_->set_motion_load_failed_callback([this](const std::filesystem::path& asset_path) {
        if (motion_loading_changed_callback_) {
            motion_loading_changed_callback_(false);
        }
        QMessageBox::warning(this,
                             "Load Failed",
                             "Failed to load motion:\n" + QString::fromStdWString(asset_path.wstring()));
    });
    motion_converter_->set_conversion_succeeded_callback([this]() {
        set_conversion_active(AssetPanelMode::Motions, false);
        refresh_motion_list();
    });
    motion_converter_->set_conversion_failed_callback([this](const std::string&) {
        set_conversion_active(AssetPanelMode::Motions, false);
        show_conversion_failure(this, AssetPanelMode::Motions);
    });

    garment_converter_->set_conversion_succeeded_callback([this]() {
        set_conversion_active(AssetPanelMode::Garments, false);
        refresh_garment_list();
    });
    garment_converter_->set_conversion_failed_callback([this](const std::string&) {
        set_conversion_active(AssetPanelMode::Garments, false);
        show_conversion_failure(this, AssetPanelMode::Garments);
    });

    update_expanded_state();
    refresh_motion_list();
    refresh_garment_list();
}

AssetBrowserPanel::~AssetBrowserPanel()
{
    asset_loader_->set_motion_loaded_callback({});
    asset_loader_->set_motion_load_failed_callback({});
    motion_converter_->set_conversion_succeeded_callback({});
    motion_converter_->set_conversion_failed_callback({});
    garment_converter_->set_conversion_succeeded_callback({});
    garment_converter_->set_conversion_failed_callback({});
}

// Asset operations
void AssetBrowserPanel::refresh_motion_list()
{
    auto motion_source_paths = asset_io::scan_asset_paths(project_paths_.amass_dir, ".npz");
    motion_source_paths.erase(std::remove_if(motion_source_paths.begin(),
                                             motion_source_paths.end(),
                                             [](const auto& path) { return !is_amass_motion_path(path); }),
                              motion_source_paths.end());

    auto motion_asset_paths = asset_io::scan_asset_paths(project_paths_.motion_asset_dir, ".motion");
    motion_asset_paths.erase(std::remove(motion_asset_paths.begin(),
                                         motion_asset_paths.end(),
                                         project_paths_.default_character_path),
                             motion_asset_paths.end());
    const auto compare_motion_subject = [](const auto& lhs, const auto& rhs) {
        return motion_subject_number(lhs) < motion_subject_number(rhs);
    };
    std::stable_sort(motion_source_paths.begin(), motion_source_paths.end(), compare_motion_subject);
    std::stable_sort(motion_asset_paths.begin(), motion_asset_paths.end(), compare_motion_subject);

    set_motion_paths(std::move(motion_source_paths), std::move(motion_asset_paths));
}

void AssetBrowserPanel::refresh_garment_list()
{
    set_garment_paths(asset_io::scan_asset_paths(project_paths_.garment_asset_dir, ".garment"));
}

void AssetBrowserPanel::load_motion(const std::filesystem::path& asset_path)
{
    if (motion_loading_changed_callback_) {
        motion_loading_changed_callback_(true);
    }
    asset_loader_->load_motion(asset_path);
}

void AssetBrowserPanel::load_garment(const std::filesystem::path& asset_path)
{
    if (garment_load_started_callback_) {
        garment_load_started_callback_();
    }

    GarmentMesh mesh;
    if (!asset_io::read_garment_mesh(asset_path, mesh)) {
        QMessageBox::warning(this,
                             "Load Failed",
                             "Failed to load garment:\n" + QString::fromStdWString(asset_path.wstring()));
        return;
    }

    if (garment_loaded_callback_) {
        garment_loaded_callback_(asset_path, std::move(mesh));
    }
}

void AssetBrowserPanel::request_garment_conversion()
{
    const QString selected_file =
        QFileDialog::getOpenFileName(this,
                                     "Select Garment OBJ",
                                     QString::fromStdWString(project_paths_.garment_source_dir.wstring()),
                                     "Garment OBJ (*.obj)");

    if (selected_file.isEmpty()) {
        return;
    }

    const std::filesystem::path garment_obj_path = selected_file.toStdWString();
    const auto garment_asset_path = asset_io::make_garment_asset_path(project_paths_, garment_obj_path);
    if (std::filesystem::exists(garment_asset_path)) {
        return;
    }

    const auto settings = select_garment_conversion_settings(this);
    if (!settings) {
        return;
    }

    const ConverterCommand command =
        asset_converter_commands::make_garment_command(project_paths_,
                                                       garment_obj_path,
                                                       garment_asset_path,
                                                       settings->attachment_type,
                                                       settings->garment_category);

    set_conversion_active(AssetPanelMode::Garments, true);
    garment_converter_->start_conversion(command);
}

void AssetBrowserPanel::request_motion_conversion(const std::filesystem::path& source_path)
{
    if (motion_converter_->is_running()) {
        return;
    }

    const auto motion_asset_path =
        project_paths_.motion_asset_dir / (source_path.stem().string() + ".motion");
    if (std::filesystem::exists(motion_asset_path)) {
        refresh_motion_list();
        return;
    }

    const ConverterCommand command =
        asset_converter_commands::make_motion_command(project_paths_, source_path, motion_asset_path);

    converting_motion_path_ = QString::fromStdWString(source_path.wstring());
    set_conversion_active(AssetPanelMode::Motions, true);
    motion_converter_->start_conversion(command);
}

void AssetBrowserPanel::set_motion_loading_changed_callback(MotionLoadingChangedCallback callback)
{
    motion_loading_changed_callback_ = std::move(callback);
}

void AssetBrowserPanel::set_motion_loaded_callback(MotionLoadedCallback callback)
{
    motion_loaded_callback_ = std::move(callback);
}

void AssetBrowserPanel::set_garment_load_started_callback(GarmentLoadStartedCallback callback)
{
    garment_load_started_callback_ = std::move(callback);
}

void AssetBrowserPanel::set_garment_loaded_callback(GarmentLoadedCallback callback)
{
    garment_loaded_callback_ = std::move(callback);
}

// panel 상태 전환 //
// open_list -> update_expanded_state
void AssetBrowserPanel::open_list(AssetPanelMode mode)
{
    panel_mode_ = mode;
    if (mode == AssetPanelMode::Motions) {
        selected_subject_id_.clear();
    }
    rebuild_list();

    if (expanded_) {
        update_expanded_state();
    } else {
        set_expanded(true);
    }
}

void AssetBrowserPanel::update_expanded_state()
{
    toggle_button_->setChecked(expanded_ && panel_mode_ == AssetPanelMode::Motions);
    garment_button_->setChecked(expanded_ && panel_mode_ == AssetPanelMode::Garments);
    expanded_panel_->setVisible(expanded_);
}

void AssetBrowserPanel::set_expanded(bool expanded)
{
    if (expanded_ == expanded) {
        return;
    }

    expanded_ = expanded;
    update_expanded_state();

    if (expansion_changed_callback_) {
        expansion_changed_callback_();
    }
}

void AssetBrowserPanel::set_expansion_changed_callback(std::function<void()> callback)
{
    expansion_changed_callback_ = std::move(callback);
}

void AssetBrowserPanel::set_motion_selection_enabled(bool enabled)
{
    toggle_button_->setEnabled(enabled);
    toggle_button_->setToolTip(enabled ? "Motion" : "Confirm or cancel garment placement first.");
}

void AssetBrowserPanel::set_garment_selection_enabled(bool enabled)
{
    if (!enabled && expanded_ && panel_mode_ == AssetPanelMode::Garments) {
        set_expanded(false);
    }
    garment_button_->setEnabled(enabled);
    garment_button_->setToolTip(enabled ? "Garment" : "Up to two garments can be added.");
}

// list 표시 //
void AssetBrowserPanel::set_motion_paths(std::vector<std::filesystem::path> source_paths,
                                         std::vector<std::filesystem::path> asset_paths)
{
    const int scroll_value = table_widget_->verticalScrollBar()->value();
    motion_source_paths_ = std::move(source_paths);
    converted_motion_paths_.clear();
    for (const auto& asset_path : asset_paths) {
        converted_motion_paths_.insert(make_motion_id(asset_path),
                                       QString::fromStdWString(asset_path.wstring()));
    }

    if (panel_mode_ == AssetPanelMode::Motions) {
        rebuild_list();
        table_widget_->verticalScrollBar()->setValue(scroll_value);
    }
}

void AssetBrowserPanel::set_garment_paths(std::vector<std::filesystem::path> asset_paths)
{
    garment_asset_paths_ = std::move(asset_paths);
    if (panel_mode_ == AssetPanelMode::Garments) {
        rebuild_list();
    }
}

void AssetBrowserPanel::rebuild_list()
{
    table_widget_->clear();
    import_button_->setVisible(panel_mode_ == AssetPanelMode::Garments);

    if (panel_mode_ == AssetPanelMode::Garments) {
        subject_back_button_->setVisible(false);
        rebuild_garment_list();
    } else if (selected_subject_id_.isEmpty()) {
        subject_back_button_->setVisible(false);
        rebuild_subject_list();
    } else {
        subject_back_button_->setVisible(true);
        rebuild_motion_list();
    }
    update_import_button_state();
}

void AssetBrowserPanel::rebuild_subject_list()
{
    std::vector<QString> subject_ids;
    QSet<QString> seen_subjects;
    for (const auto& source_path : motion_source_paths_) {
        const QString subject_id = make_subject_id(source_path);
        if (!seen_subjects.contains(subject_id)) {
            seen_subjects.insert(subject_id);
            subject_ids.push_back(subject_id);
        }
    }

    title_label_->set_full_text(QString("Available subjects (%1)").arg(static_cast<int>(subject_ids.size())));
    table_widget_->setColumnCount(2);
    table_widget_->setRowCount(static_cast<int>(subject_ids.size()));
    table_widget_->setHorizontalHeaderLabels({"Subject", "Subject Description"});
    table_widget_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_widget_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    for (int row = 0; row < static_cast<int>(subject_ids.size()); ++row) {
        const QString& subject_id = subject_ids[row];
        auto* id_item = new QTableWidgetItem(subject_id);
        id_item->setData(Qt::UserRole, subject_id);
        id_item->setTextAlignment(Qt::AlignCenter);
        table_widget_->setItem(row, 0, id_item);
        const QString description = subject_descriptions_.value(subject_id);
        auto* description_item = new QTableWidgetItem(QString(" ") + description);
        description_item->setToolTip(description);
        table_widget_->setItem(row, 1, description_item);
    }
}

void AssetBrowserPanel::rebuild_motion_list()
{
    std::vector<const std::filesystem::path*> subject_motion_paths;
    for (const auto& source_path : motion_source_paths_) {
        if (make_subject_id(source_path) == selected_subject_id_) {
            subject_motion_paths.push_back(&source_path);
        }
    }

    const QString subject_title = subject_descriptions_.value(selected_subject_id_, selected_subject_id_);
    title_label_->set_full_text(subject_title);
    table_widget_->setColumnCount(3);
    table_widget_->setRowCount(static_cast<int>(subject_motion_paths.size()));
    table_widget_->setHorizontalHeaderLabels({"ID", "Motion Description", ""});
    table_widget_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_widget_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_widget_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table_widget_->setColumnWidth(2, 42);

    const QColor unavailable_color{"#8a8a8a"};
    for (int row = 0; row < static_cast<int>(subject_motion_paths.size()); ++row) {
        const std::filesystem::path& source_path = *subject_motion_paths[row];
        const QString source_path_string = QString::fromStdWString(source_path.wstring());
        const QString motion_id = make_motion_id(source_path);
        const QString motion_asset_path = converted_motion_paths_.value(motion_id);
        const bool is_converted = !motion_asset_path.isEmpty();

        auto* id_item = new QTableWidgetItem(motion_id);
        id_item->setData(Qt::UserRole, motion_asset_path);
        id_item->setToolTip(is_converted ? motion_asset_path : source_path_string);
        const QString description = motion_descriptions_.value(motion_id);
        auto* description_item = new QTableWidgetItem(QString(" ") + description);
        description_item->setToolTip(description);
        if (!is_converted) {
            id_item->setForeground(unavailable_color);
            description_item->setForeground(unavailable_color);
        }
        table_widget_->setItem(row, 0, id_item);
        table_widget_->setItem(row, 1, description_item);

        if (motion_conversion_active_ && source_path_string == converting_motion_path_) {
            table_widget_->setCellWidget(row, 2, make_centered_cell_widget(new LoadingSpinner()));
        } else if (!is_converted) {
            auto* convert_button = new QPushButton();
            convert_button->setFixedSize(24, 24);
            convert_button->setIcon(make_plus_icon());
            convert_button->setIconSize(QSize{action_icon_size, action_icon_size});
            convert_button->setStyleSheet("QPushButton {"
                                          "  background-color: #1f6feb;"
                                          "  color: white;"
                                          "  border: 1px solid #1158c7;"
                                          "  border-radius: 4px;"
                                          "  padding: 0;"
                                          "}"
                                          "QPushButton:hover {"
                                          "  background-color: #2f81f7;"
                                          "}"
                                          "QPushButton:disabled {"
                                          "  background-color: #8caee6;"
                                          "  color: #dddddd;"
                                          "}");
            convert_button->setToolTip("Motion convert");
            convert_button->setEnabled(!motion_conversion_active_);
            connect(convert_button, &QPushButton::clicked, this, [this, source_path]() {
                if (motion_conversion_active_) {
                    return;
                }
                request_motion_conversion(source_path);
            });
            table_widget_->setCellWidget(row, 2, make_centered_cell_widget(convert_button));
        }
    }
}

void AssetBrowserPanel::rebuild_garment_list()
{
    title_label_->set_full_text(
        QString("Available garments (%1)").arg(static_cast<int>(garment_asset_paths_.size())));
    table_widget_->setColumnCount(1);
    table_widget_->setRowCount(static_cast<int>(garment_asset_paths_.size()));
    table_widget_->setHorizontalHeaderLabels({"Garment"});
    table_widget_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

    for (int row = 0; row < static_cast<int>(garment_asset_paths_.size()); ++row) {
        const auto& asset_path = garment_asset_paths_[row];
        const QString full_path = QString::fromStdWString(asset_path.wstring());
        auto* name_item = new QTableWidgetItem(QString(" ") + make_asset_display_name(asset_path));
        name_item->setData(Qt::UserRole, full_path);
        name_item->setToolTip(full_path);
        table_widget_->setItem(row, 0, name_item);
    }
}

void AssetBrowserPanel::select_table_row(int row)
{
    const QTableWidgetItem* item = table_widget_->item(row, 0);
    if (!item) {
        return;
    }

    if (panel_mode_ == AssetPanelMode::Motions && selected_subject_id_.isEmpty()) {
        selected_subject_id_ = item->data(Qt::UserRole).toString();
        rebuild_list();
        return;
    }

    const QString asset_path = item->data(Qt::UserRole).toString();
    if (asset_path.isEmpty()) {
        return;
    }

    if (panel_mode_ == AssetPanelMode::Motions) {
        load_motion(asset_path.toStdWString());
    } else {
        load_garment(asset_path.toStdWString());
    }
}

// add button //
void AssetBrowserPanel::set_conversion_active(AssetPanelMode mode, bool active)
{
    if (mode == AssetPanelMode::Motions) {
        motion_conversion_active_ = active;
        if (!active) {
            converting_motion_path_.clear();
        }
        if (panel_mode_ == AssetPanelMode::Motions) {
            const int scroll_value = table_widget_->verticalScrollBar()->value();
            rebuild_list();
            table_widget_->verticalScrollBar()->setValue(scroll_value);
        }
    } else {
        garment_conversion_active_ = active;
        update_import_button_state();
    }
}

void AssetBrowserPanel::update_import_button_state()
{
    import_button_->setEnabled(!garment_conversion_active_);
}

// getter //
bool AssetBrowserPanel::is_expanded() const
{
    return expanded_;
}
