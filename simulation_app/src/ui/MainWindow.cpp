#include "ui/MainWindow.h"

#include "rendering/OpenGLViewerWidget.h"
#include "ui/MotionBrowserPanel.h"

#include <algorithm>
#include <iostream>

#include <QEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QPoint>
#include <QRect>
#include <QResizeEvent>
#include <QSize>
#include <QWidget>

namespace {
int clamp_int(int value, int min_value, int max_value)
{
    return std::max(min_value, std::min(value, max_value));
}

QString to_q_string(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

void show_conversion_failure(QWidget* parent)
{
    QMessageBox::warning(
        parent,
        "Conversion Failed",
        "Failed to convert AMASS motion."
    );
}
}

MainWindow::MainWindow(const std::filesystem::path& project_root, QWidget* parent)
    : QMainWindow(parent)
    , project_paths_(make_project_paths(project_root))
{
    setWindowTitle("SIMULATION APP");
    resize(1440, 900);

    viewer_container_ = new QWidget(this);
    viewer_widget_ = new OpenGLViewerWidget(viewer_container_);
    browser_panel_ = new MotionBrowserPanel(viewer_container_);

    setCentralWidget(viewer_container_);
    viewer_container_->installEventFilter(this);
    
    // motion 선택 toggle 관련 event들의 callback 함수 설정
    browser_panel_->raise();
    browser_panel_->set_motion_selected_callback([this](const std::filesystem::path& motion_asset_path) {
        load_motion_asset(motion_asset_path);
    });
    browser_panel_->set_motion_import_button_callback([this]() {
        request_amass_conversion();
    });
    browser_panel_->set_garment_button_callback([this]() {
        request_garment_asset_selection();
    });
    browser_panel_->set_expansion_changed_callback([this]() {
        update_motion_browser();
        browser_panel_->raise();
    });

    refresh_motion_list();
    update_motion_browser();
}

MainWindow::~MainWindow()
{
    if (!converter_process_) {
        return;
    }

    converter_process_->terminate();
    if (!converter_process_->waitForFinished(1000)) {
        converter_process_->kill();
        converter_process_->waitForFinished(1000);
    }
}

// Layout

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == viewer_container_ && event->type() == QEvent::Resize) {
        update_motion_browser();
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    update_motion_browser();
}

void MainWindow::update_motion_browser()
{
    if (!viewer_container_ || !viewer_widget_ || !browser_panel_) {
        return;
    }

    const QSize container_size = viewer_container_->size();
    viewer_widget_->setGeometry(QRect(QPoint(0, 0), container_size));

    constexpr int margin = 12;
    constexpr int expanded_width = 340;
    constexpr int min_expanded_height = 180;
    constexpr int max_expanded_height = 280;

    const QSize collapsed_size = browser_panel_->sizeHint();
    const int available_width = std::max(0, container_size.width() - margin * 2);
    const int available_height = std::max(0, container_size.height() - margin * 2);

    int overlay_width = std::min(collapsed_size.width(), available_width);
    int overlay_height = std::min(collapsed_size.height(), available_height);

    if (browser_panel_->is_expanded()) {
        overlay_width = std::min(expanded_width, available_width);

        const int target_height = container_size.height() / 4;
        const int expanded_max_height = std::min(max_expanded_height, available_height);
        const int expanded_height = clamp_int(
            target_height,
            std::min(min_expanded_height, expanded_max_height),
            expanded_max_height
        );
        overlay_height = expanded_height;
    }

    browser_panel_->setGeometry(margin, margin, overlay_width, overlay_height);
    browser_panel_->raise();
}


// Motion assets

void MainWindow::refresh_motion_list()
{
    motions_ = scan_motion_assets(project_paths_);
    browser_panel_->set_motion_list(motions_);
}

void MainWindow::load_motion_asset(const std::filesystem::path& motion_asset_path)
{
    if (!viewer_widget_->load_motion_asset(motion_asset_path)) {
        QMessageBox::warning(
            this,
            "Load Failed",
            "Failed to load motion:\n" + to_q_string(motion_asset_path)
        );
        return;
    }

    browser_panel_->set_current_motion_asset(motion_asset_path);
}

void MainWindow::request_garment_asset_selection()
{
    const QString selected_file = QFileDialog::getOpenFileName(
        this,
        "Select Garment",
        to_q_string(project_paths_.garment_asset_dir),
        "Garment OBJ (*.obj);;All Files (*)"
    );

    if (selected_file.isEmpty()) {
        return;
    }

    const std::filesystem::path garment_asset_path = selected_file.toStdWString();
    if (!viewer_widget_->load_garment_asset(garment_asset_path)) {
        QMessageBox::warning(
            this,
            "Load Failed",
            "Failed to load garment:\n" + to_q_string(garment_asset_path)
        );
    }
}

// AMASS conversion

// AMASS motion 변환 요청
void MainWindow::request_amass_conversion()
{
    // 중복 실행 방지: 현재 변환 작업 중-> 종료
    if (converter_process_) {
        return;
    }

    // python converter 실행에 필요한 command 준비
    const std::optional<ConverterCommand> command = prepare_amass_conversion();
    if (!command) {
        return;
    }

    // 변환 작업 시작
    start_converter_process(*command);
}

std::optional<ConverterCommand> MainWindow::prepare_amass_conversion()
{   
    // 파일 선택 창 열고, motion 선택
    const std::filesystem::path default_dir = project_paths_.amass_dir;
    const QString selected_file = QFileDialog::getOpenFileName(
        this,
        "Select AMASS Motion",
        to_q_string(default_dir),
        "AMASS Motion (*.npz)"
    );

    if (selected_file.isEmpty()) {
        return std::nullopt;
    }

    const std::filesystem::path amass_motion_path = selected_file.toStdWString();
    const std::filesystem::path motion_asset_path = make_motion_asset_path(project_paths_, amass_motion_path);


    // 이미 변환된 motion -> 변환 없음
    if (std::filesystem::exists(motion_asset_path)) {
        refresh_motion_list();
        return std::nullopt;
    }

    // python converter 실행에 필요한 command 준비
    const ConverterCommand command = make_converter_command(project_paths_, amass_motion_path, motion_asset_path);
    if (!command.ok) {
        std::cerr << command.error_message << '\n';
        show_conversion_failure(this);
        return std::nullopt;
    }

    return command;
}

void MainWindow::start_converter_process(const ConverterCommand& command)
{
    converter_result_ = {};
    converter_process_ = new QProcess(this);
    converter_process_->setProgram(command.program);
    converter_process_->setArguments(command.arguments);
    converter_process_->setWorkingDirectory(command.working_directory);

    setup_converter_callbacks();
    browser_panel_->set_conversion_active(true);
    converter_process_->start();
}

void MainWindow::setup_converter_callbacks()
{   
    connect(converter_process_, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!converter_process_) {
            return;
        }

        std::cout << converter_process_->readAllStandardOutput().toStdString();
    });

    connect(converter_process_, &QProcess::readyReadStandardError, this, [this]() {
        if (!converter_process_) {
            return;
        }

        std::cerr << converter_process_->readAllStandardError().toStdString();
    });

    // 변환 process 종료 시 callback: 변환 결과 저장
    connect(
        converter_process_,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this](int exit_code, QProcess::ExitStatus exit_status) {
            finish_converter_process(exit_code, exit_status);
        }
    );

    // 변환 process 실패 시 callback: 에러 메시지 저장
    connect(converter_process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!converter_process_) {
            return;
        }

        converter_result_.error_message = converter_process_->errorString().toStdString();
        if (error == QProcess::FailedToStart) {
            finish_converter_process(-1, QProcess::CrashExit);
        }
    });
}

void MainWindow::finish_converter_process(int exit_code, QProcess::ExitStatus exit_status)
{
    if (!converter_process_) {
        return;
    }

    converter_result_.exit_code = exit_code;

    std::cout << converter_process_->readAllStandardOutput().toStdString();
    std::cerr << converter_process_->readAllStandardError().toStdString();

    // 성공/실패 판정
    converter_result_.ok = exit_status == QProcess::NormalExit && exit_code == 0;
    if (!converter_result_.ok && converter_result_.error_message.empty()) {
        converter_result_.error_message = exit_status == QProcess::NormalExit
            ? "Converter failed with exit code: " + std::to_string(exit_code)
            : "Converter process crashed.";
    }

    // process 객체 정리
    QProcess* finished_process = converter_process_;
    converter_process_ = nullptr;
    finished_process->deleteLater();

    // 변환 버튼 다시 활성화 & 변환 모션 추가된 UI로 update
    browser_panel_->set_conversion_active(false);

    if (converter_result_.ok) {
        refresh_motion_list();
    } else {
        std::cerr << converter_result_.error_message << '\n';
        show_conversion_failure(this);
    }

    converter_result_ = {};
}
