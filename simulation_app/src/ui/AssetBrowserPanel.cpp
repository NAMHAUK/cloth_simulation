#include "ui/AssetBrowserPanel.h"

#include "asset/AssetConverter.h"
#include "asset/AssetIO.h"
#include "ui/ElidedLabel.h"
#include "ui/MotionCard.h"
#include "utils/QtUtils.h"

#include <algorithm>
#include <exception>
#include <optional>
#include <string>

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
#include <QMessageBox>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QScrollBar>
#include <QSize>
#include <QSizePolicy>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <iostream>
#include <utility>

namespace {
constexpr int close_icon_size = 12;
constexpr int action_icon_size = 20;
constexpr int garment_column_width = 369;

std::optional<QString> select_garment_category(QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle("Garment Conversion Settings");

    QComboBox garment_category;
    garment_category.addItem("Top", "top");
    garment_category.addItem("Bottom", "bottom");
    garment_category.addItem("Full-Body", "full-body");

    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QFormLayout layout(&dialog);
    layout.addRow("Garment Category", &garment_category);
    layout.addRow(&buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    return garment_category.currentData().toString();
}

QString make_motion_id(const std::filesystem::path& motion_path)
{
    QString motion_id = to_q_string(motion_path.stem());
    const QString pose_suffix = "_poses";
    if (motion_id.endsWith(pose_suffix)) {
        motion_id.chop(pose_suffix.size());
    }
    return motion_id;
}

QString format_subject_id(int subject_id)
{
    return QString::number(subject_id).rightJustified(2, '0');
}

QJsonObject read_descriptions(const std::filesystem::path& catalog_path)
{
    QFile catalog_file(to_q_string(catalog_path));
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

    return document.object();
}

class LoadingSpinner final : public QWidget
{
public:
    explicit LoadingSpinner(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedSize(18, 18);
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

QWidget* make_motion_conversion_cell(QWidget* content)
{
    auto* container = new QWidget();
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch(1);
    layout->addWidget(content);
    layout->addStretch(1);
    return container;
}

}

AssetBrowserPanel::AssetBrowserPanel(const ProjectPaths& project_paths, QWidget* parent)
    : QWidget(parent),
      project_paths_(project_paths),
      motion_descriptions_(read_descriptions(project_paths_.motion_asset_dir / "motion_catalog.json")),
      subject_descriptions_(read_descriptions(project_paths_.motion_asset_dir / "subject_catalog.json"))
{
    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(6);

    setup_asset_buttons(*root_layout);
    setup_list_panel(*root_layout);
    setup_motion_card(*root_layout);
    setup_asset_converters();

    load_motion_paths();
    refresh_garment_list();
}

// Initialization
void AssetBrowserPanel::setup_asset_buttons(QVBoxLayout& root_layout)
{
    auto* button_layout = new QHBoxLayout();
    button_layout->setContentsMargins(0, 0, 0, 0);
    button_layout->setSpacing(6);

    motion_button_ = new QPushButton(this);
    motion_button_->setIcon(QIcon{QStringLiteral(":/icons/motion.png")});
    motion_button_->setIconSize(QSize(76, 76));
    motion_button_->setToolTip("Motion");
    motion_button_->setCheckable(true);
    motion_button_->setFixedSize(86, 86);
    motion_button_->setProperty("role", "assetButton");

    garment_button_ = new QPushButton(this);
    garment_button_->setIcon(QIcon{QStringLiteral(":/icons/garment.png")});
    garment_button_->setIconSize(QSize(76, 76));
    garment_button_->setToolTip("Garment");
    garment_button_->setCheckable(true);
    garment_button_->setFixedSize(86, 86);
    garment_button_->setProperty("role", "assetButton");

    button_layout->addWidget(motion_button_);
    button_layout->addWidget(garment_button_);
    button_layout->addStretch(1);
    root_layout.addLayout(button_layout);

    connect(motion_button_, &QPushButton::clicked, this, &AssetBrowserPanel::toggle_motion_list);
    connect(garment_button_, &QPushButton::clicked, this, &AssetBrowserPanel::toggle_garment_list);
}

void AssetBrowserPanel::setup_list_panel(QVBoxLayout& root_layout)
{
    list_panel_ = new QWidget(this);
    list_panel_->setObjectName("assetBrowserExpandedPanel");
    list_panel_->setVisible(false);

    auto* panel_layout = new QVBoxLayout(list_panel_);
    panel_layout->setContentsMargins(10, 10, 10, 10);
    panel_layout->setSpacing(8);

    auto* header_layout = new QHBoxLayout();
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->setSpacing(6);

    subject_back_button_ = new QPushButton(QString("%1 Subjects /").arg(QChar(0x2039)), list_panel_);
    subject_back_button_->setObjectName("subjectBackButton");
    subject_back_button_->setCursor(Qt::PointingHandCursor);
    subject_back_button_->setMinimumHeight(26);
    subject_back_button_->setToolTip("Back to subjects list");

    auto* close_button = new QPushButton(list_panel_);
    close_button->setObjectName("assetBrowserCloseButton");
    close_button->setFixedSize(26, 26);
    close_button->setIcon(QIcon{QStringLiteral(":/icons/close.svg")});
    close_button->setIconSize(QSize{close_icon_size, close_icon_size});
    close_button->setToolTip("cancel");

    title_label_ = new ElidedLabel(QString{}, list_panel_);
    title_label_->setObjectName("assetBrowserTitle");
    title_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    table_widget_ = new QTableWidget(list_panel_);
    table_widget_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_widget_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_widget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_widget_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table_widget_->verticalHeader()->setVisible(false);

    import_button_ = new QPushButton(list_panel_);
    import_button_->setMinimumHeight(32);
    import_button_->setIcon(QIcon{QStringLiteral(":/icons/file-import-solid-full.svg")});
    import_button_->setIconSize(QSize{action_icon_size, action_icon_size});
    import_button_->setProperty("role", "primaryAction");
    import_button_->setToolTip("Import garment");

    header_layout->addWidget(subject_back_button_);
    header_layout->addWidget(title_label_, 1);
    header_layout->addWidget(close_button);
    panel_layout->addLayout(header_layout);
    panel_layout->addWidget(table_widget_, 1);
    panel_layout->addWidget(import_button_);
    root_layout.addWidget(list_panel_, 1);

    connect(subject_back_button_, &QPushButton::clicked, this, &AssetBrowserPanel::return_to_subject_list);
    connect(close_button, &QPushButton::clicked, this, [this]() { set_state(State::Closed); });
    connect(table_widget_, &QTableWidget::cellClicked, this, &AssetBrowserPanel::handle_table_row_click);
    connect(import_button_, &QPushButton::clicked, this, &AssetBrowserPanel::request_garment_conversion);
}

void AssetBrowserPanel::setup_motion_card(QVBoxLayout& root_layout)
{
    motion_card_ = new MotionCard(this);
    root_layout.addWidget(motion_card_);
    connect(motion_card_, &MotionCard::motion_activated, this, &AssetBrowserPanel::open_motion);
    connect(motion_card_, &MotionCard::layout_changed, this, &AssetBrowserPanel::update_motion_card_layout);
}

void AssetBrowserPanel::setup_asset_converters()
{
    motion_converter_ = new AssetConverter(project_paths_, this);
    garment_converter_ = new AssetConverter(project_paths_, this);

    connect(motion_converter_, &AssetConverter::conversion_succeeded, this, [this]() {
        const QString motion_id = make_motion_id(converting_motion_asset_path_);
        converted_motion_paths_.insert(motion_id, to_q_string(converting_motion_asset_path_));
        motion_card_->show_motion(converting_motion_asset_path_,
                                  motion_id,
                                  motion_descriptions_.value(motion_id).toString());
        finish_motion_conversion();
    });
    connect(motion_converter_, &AssetConverter::conversion_failed, this, [this]() {
        finish_motion_conversion();
        QMessageBox::warning(this, "Conversion Failed", "Failed to convert AMASS motion.");
    });

    connect(garment_converter_, &AssetConverter::conversion_succeeded, this, [this]() {
        import_button_->setEnabled(true);
        refresh_garment_list();
    });
    connect(garment_converter_, &AssetConverter::conversion_failed, this, [this]() {
        import_button_->setEnabled(true);
        QMessageBox::warning(this, "Conversion Failed", "Failed to convert garment OBJ.");
    });
}

void AssetBrowserPanel::load_motion_paths()
{
    // load all AMASS motion data
    MotionSubject* current_subject = nullptr;
    for (auto& source_path : asset_io::scan_asset_paths(project_paths_.amass_dir, ".npz")) {
        const int subject_id = make_motion_id(source_path).section('_', 0, 0).toInt();
        if (subject_id <= 0) {
            continue;
        }

        if (!current_subject || current_subject->id != subject_id) {
            current_subject = &subjects_.emplace_back(MotionSubject{subject_id, {}});
        }
        current_subject->motion_paths.push_back(std::move(source_path));
    }

    // sort subjects by ID
    std::sort(subjects_.begin(), subjects_.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.id < rhs.id;
    });

    // load converted motion data
    for (const auto& asset_path : asset_io::scan_asset_paths(project_paths_.motion_asset_dir, ".motion")) {
        if (asset_path != project_paths_.default_character_path) {
            converted_motion_paths_.insert(make_motion_id(asset_path), to_q_string(asset_path));
        }
    }
}

// UI Updates
void AssetBrowserPanel::set_motion_selection_enabled(bool enabled)
{
    motion_button_->setEnabled(enabled);
    motion_card_->setEnabled(enabled);
    motion_button_->setToolTip(enabled ? "Motion" : "Confirm or cancel garment placement first.");
}

void AssetBrowserPanel::set_garment_selection_enabled(bool enabled)
{
    if (!enabled && state_ == State::Garments) {
        set_state(State::Closed);
    }
    garment_button_->setEnabled(enabled);
    garment_button_->setToolTip(enabled ? "Garment" : "Up to two garments can be added.");
}

// Panel Interaction
void AssetBrowserPanel::toggle_motion_list()
{
    const bool is_open = state_ == State::Subjects || state_ == State::Motions;
    set_state(is_open ? State::Closed : State::Subjects);
}

void AssetBrowserPanel::toggle_garment_list()
{
    set_state(state_ == State::Garments ? State::Closed : State::Garments);
}

void AssetBrowserPanel::return_to_subject_list()
{
    const int previous_subject_index = selected_subject_index_;
    set_state(State::Subjects);

    QTableWidgetItem* subject_item = table_widget_->item(previous_subject_index, 0);
    table_widget_->setCurrentItem(subject_item);
    table_widget_->scrollToItem(subject_item, QAbstractItemView::PositionAtCenter);
}

void AssetBrowserPanel::open_motion(const std::filesystem::path& asset_path)
{
    const int subject_id = make_motion_id(asset_path).section('_', 0, 0).toInt();
    const auto subject = std::find_if(subjects_.begin(), subjects_.end(), [subject_id](const auto& entry) {
        return entry.id == subject_id;
    });
    if (subject == subjects_.end()) {
        return;
    }

    selected_subject_index_ = static_cast<int>(subject - subjects_.begin());
    set_state(State::Motions);
    const QString asset_path_text = to_q_string(asset_path);
    for (int row = 0; row < table_widget_->rowCount(); ++row) {
        const auto* item = table_widget_->item(row, 0);
        if (item->data(Qt::UserRole).toString() == asset_path_text) {
            table_widget_->selectRow(row);
            table_widget_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
            load_motion(asset_path);
            return;
        }
    }
}

void AssetBrowserPanel::handle_table_row_click(int row)
{
    const QTableWidgetItem* item = table_widget_->item(row, 0);
    if (!item) {
        return;
    }
    const std::filesystem::path asset_path = item->data(Qt::UserRole).toString().toStdWString();

    switch (state_) {
    case State::Subjects:
        selected_subject_index_ = row;
        set_state(State::Motions);
        break;
    case State::Motions:
        if (!asset_path.empty()) {
            load_motion(asset_path);
        }
        break;
    case State::Garments:
        if (!asset_path.empty()) {
            load_garment(asset_path);
        }
        break;
    case State::Closed:
        break;
    }
}

void AssetBrowserPanel::set_state(State state)
{
    state_ = state;
    const bool is_expanded = state_ != State::Closed;
    const bool is_motion_list = state_ == State::Subjects || state_ == State::Motions;

    motion_button_->setChecked(is_motion_list);
    garment_button_->setChecked(state_ == State::Garments);
    list_panel_->setVisible(is_expanded);
    if (is_expanded) {
        rebuild_list();
    }

    update_motion_card_layout();
}

// Asset Operations
void AssetBrowserPanel::refresh_motion_list()
{
    if (state_ == State::Subjects || state_ == State::Motions) {
        const int scroll_value = table_widget_->verticalScrollBar()->value();
        rebuild_list();
        table_widget_->verticalScrollBar()->setValue(scroll_value);
    }
}

void AssetBrowserPanel::refresh_garment_list()
{
    garment_asset_paths_ = asset_io::scan_asset_paths(project_paths_.garment_asset_dir, ".garment");
    if (state_ == State::Garments) {
        rebuild_list();
    }
}

void AssetBrowserPanel::load_motion(const std::filesystem::path& asset_path)
{
    auto on_loaded = [this, asset_path](CharacterMotion motion) {
        if (motion_loaded_callback_(std::move(motion))) {
            motion_card_->dismiss(asset_path);
        }
        Q_EMIT motion_loading_changed(false);
    };
    auto on_failed = [this, asset_path] {
        Q_EMIT motion_loading_changed(false);
        QMessageBox::warning(this, "Load Failed", "Failed to load motion:\n" + to_q_string(asset_path));
    };

    Q_EMIT motion_loading_changed(true);
    motion_load_ = QtConcurrent::run(asset_io::read_character_motion, asset_path)
                       .then(this, std::move(on_loaded))
                       .onFailed(this, std::move(on_failed));
}

void AssetBrowserPanel::load_garment(const std::filesystem::path& asset_path)
{
    try {
        GarmentMesh mesh = asset_io::read_garment_mesh(asset_path);
        garment_loaded_callback_(asset_path, std::move(mesh));
    } catch (const std::exception&) {
        QMessageBox::warning(this, "Load Failed", "Failed to load garment:\n" + to_q_string(asset_path));
    }
}

void AssetBrowserPanel::request_garment_conversion()
{
    const QString selected_file = QFileDialog::getOpenFileName(this,
                                                               "Select Garment OBJ",
                                                               to_q_string(project_paths_.garment_source_dir),
                                                               "Garment OBJ (*.obj)");
    if (selected_file.isEmpty()) {
        return;
    }

    const auto garment_obj_path = std::filesystem::path{selected_file.toStdWString()};
    const auto garment_asset_path = make_garment_asset_path(project_paths_, garment_obj_path);
    if (std::filesystem::exists(garment_asset_path)) {
        return;
    }

    const auto garment_category = select_garment_category(this);
    if (!garment_category) {
        return;
    }

    import_button_->setEnabled(false);
    garment_converter_->start_garment_conversion(garment_obj_path, garment_asset_path, *garment_category);
}

void AssetBrowserPanel::request_motion_conversion(const std::filesystem::path& source_path)
{
    if (motion_converter_->is_running()) {
        return;
    }

    const auto motion_asset_path =
        project_paths_.motion_asset_dir / (source_path.stem().string() + ".motion");
    converting_motion_asset_path_ = motion_asset_path;
    refresh_motion_list();
    motion_converter_->start_motion_conversion(source_path, motion_asset_path);
}

void AssetBrowserPanel::finish_motion_conversion()
{
    converting_motion_asset_path_.clear();
    refresh_motion_list();
}

void AssetBrowserPanel::update_motion_card_layout()
{
    auto* root_layout = static_cast<QVBoxLayout*>(layout());
    root_layout->removeWidget(motion_card_);
    root_layout->insertWidget(state_ == State::Garments ? 1 : 2, motion_card_);
    Q_EMIT layout_changed();
    layout()->activate();
}

// List Display
void AssetBrowserPanel::rebuild_list()
{
    table_widget_->clear();
    import_button_->setVisible(state_ == State::Garments);
    subject_back_button_->setVisible(state_ == State::Motions);

    switch (state_) {
    case State::Subjects:
        rebuild_subject_list();
        break;
    case State::Motions:
        rebuild_motion_list();
        break;
    case State::Garments:
        rebuild_garment_list();
        break;
    case State::Closed:
        break;
    }
}

void AssetBrowserPanel::rebuild_subject_list()
{
    const int subject_count = static_cast<int>(subjects_.size());

    title_label_->set_text(QString("Available subjects (%1)").arg(subject_count));
    table_widget_->setColumnCount(2);
    table_widget_->setRowCount(subject_count);
    table_widget_->setHorizontalHeaderLabels({"Subject", "Subject Description"});

    auto* header = table_widget_->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::Stretch);

    for (int row = 0; row < subject_count; ++row) {
        const QString subject_id = format_subject_id(subjects_[row].id);
        const QString description = subject_descriptions_.value(subject_id).toString();

        auto* subject_item = new QTableWidgetItem(subject_id);
        subject_item->setTextAlignment(Qt::AlignCenter);
        table_widget_->setItem(row, 0, subject_item);

        auto* description_item = new QTableWidgetItem(" " + description);
        description_item->setToolTip(description);
        table_widget_->setItem(row, 1, description_item);
    }
}

void AssetBrowserPanel::rebuild_motion_list()
{
    const MotionSubject& subject = subjects_[selected_subject_index_];

    const int subject_motion_count = static_cast<int>(subject.motion_paths.size());
    const QString subject_id = format_subject_id(subject.id);

    title_label_->set_text(subject_descriptions_.value(subject_id).toString(subject_id));
    table_widget_->setColumnCount(3);
    table_widget_->setRowCount(subject_motion_count);
    table_widget_->setHorizontalHeaderLabels({"ID", "Motion Description", ""});

    auto* header = table_widget_->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    header->setSectionResizeMode(2, QHeaderView::Fixed);
    table_widget_->setColumnWidth(2, 30);

    const QString converting_motion_id = make_motion_id(converting_motion_asset_path_);
    for (int row = 0; row < subject_motion_count; ++row) {
        set_motion_row(row, subject.motion_paths[row], converting_motion_id);
    }
    table_widget_->scrollToTop();
}

void AssetBrowserPanel::set_motion_row(int row,
                                       const std::filesystem::path& source_path,
                                       const QString& converting_motion_id)
{
    // Motion information
    const QString motion_id = make_motion_id(source_path);
    const QString motion_asset_path = converted_motion_paths_.value(motion_id);
    const bool is_converted = !motion_asset_path.isEmpty();

    auto* motion_id_cell = new QTableWidgetItem(motion_id);
    motion_id_cell->setData(Qt::UserRole, motion_asset_path);
    motion_id_cell->setToolTip(is_converted ? motion_asset_path : to_q_string(source_path));

    const QString description = motion_descriptions_.value(motion_id).toString();
    auto* description_cell = new QTableWidgetItem(" " + description);
    description_cell->setToolTip(description);

    table_widget_->setItem(row, 0, motion_id_cell);
    table_widget_->setItem(row, 1, description_cell);

    // Conversion Status {null, loading spinner, convert button}
    if (is_converted) {
        return;
    }

    static const QColor unavailable_color{"#8a8a8a"};
    motion_id_cell->setForeground(unavailable_color);
    description_cell->setForeground(unavailable_color);

    if (motion_id == converting_motion_id) {
        table_widget_->setCellWidget(row, 2, make_motion_conversion_cell(new LoadingSpinner()));
        return;
    }

    auto* convert_button = new QPushButton();
    convert_button->setFixedSize(24, 24);
    convert_button->setIcon(QIcon{QStringLiteral(":/icons/plus.svg")});
    convert_button->setIconSize(QSize{action_icon_size, action_icon_size});
    convert_button->setProperty("role", "primaryAction");
    convert_button->setToolTip("Motion convert");
    convert_button->setEnabled(converting_motion_asset_path_.empty());

    connect(convert_button, &QPushButton::clicked, this, [this, source_path]() {
        request_motion_conversion(source_path);
    });
    table_widget_->setCellWidget(row, 2, make_motion_conversion_cell(convert_button));
}

void AssetBrowserPanel::rebuild_garment_list()
{
    const int garment_count = static_cast<int>(garment_asset_paths_.size());

    title_label_->set_text(QString("Available garments (%1)").arg(garment_count));
    table_widget_->setColumnCount(1);
    table_widget_->setRowCount(garment_count);
    table_widget_->setHorizontalHeaderLabels({"Garment"});
    table_widget_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table_widget_->setColumnWidth(0, garment_column_width);

    for (int row = 0; row < garment_count; ++row) {
        const std::filesystem::path& asset_path = garment_asset_paths_[row];
        const QString asset_path_text = to_q_string(asset_path);

        auto* garment_item = new QTableWidgetItem(" " + to_q_string(asset_path.stem()));
        garment_item->setData(Qt::UserRole, asset_path_text);
        garment_item->setToolTip(asset_path_text);
        table_widget_->setItem(row, 0, garment_item);
    }
}

// Accessors
bool AssetBrowserPanel::is_expanded() const
{
    return state_ != State::Closed;
}

int AssetBrowserPanel::motion_card_height() const
{
    return motion_card_->isHidden() ? 0 : motion_card_->height() + layout()->spacing();
}

// Callback Registration
void AssetBrowserPanel::set_motion_loaded_callback(MotionLoadedCallback callback)
{
    motion_loaded_callback_ = std::move(callback);
}

void AssetBrowserPanel::set_garment_loaded_callback(GarmentLoadedCallback callback)
{
    garment_loaded_callback_ = std::move(callback);
}
