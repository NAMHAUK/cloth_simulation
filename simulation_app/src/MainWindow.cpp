#include "MainWindow.h"

#include "MotionBrowserPanel.h"
#include "OpenGLViewerWidget.h"

#include <algorithm>

#include <QApplication>
#include <QEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QPoint>
#include <QRect>
#include <QResizeEvent>
#include <QSize>
#include <QStatusBar>
#include <QWidget>

namespace {
int clampInt(int value, int minValue, int maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

QString toQString(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

void showConversionFailure(
    QWidget* parent,
    const std::filesystem::path& motionPath,
    const ConverterResult& result)
{
    QMessageBox messageBox(parent);
    messageBox.setIcon(QMessageBox::Warning);
    messageBox.setWindowTitle("Conversion Failed");
    messageBox.setText("Failed to convert AMASS motion:\n" + toQString(motionPath));

    const QString summary = result.errorMessage.empty()
        ? "Converter failed."
        : QString::fromStdString(result.errorMessage);
    messageBox.setInformativeText(summary);

    QString details;
    if (!result.stderrText.empty()) {
        details += "stderr:\n";
        details += QString::fromStdString(result.stderrText);
        details += "\n";
    }
    if (!result.stdoutText.empty()) {
        details += "stdout:\n";
        details += QString::fromStdString(result.stdoutText);
    }
    if (!details.isEmpty()) {
        messageBox.setDetailedText(details);
    }

    messageBox.exec();
}
}

MainWindow::MainWindow(const std::filesystem::path& projectRoot, QWidget* parent)
    : QMainWindow(parent)
    , projectRoot_(projectRoot)
{
    setWindowTitle("SMPL Motion Cache Viewer");
    resize(1440, 900);

    viewerContainer_ = new QWidget(this);
    viewerWidget_ = new OpenGLViewerWidget(viewerContainer_);
    browserPanel_ = new MotionBrowserPanel(viewerContainer_);

    setCentralWidget(viewerContainer_);
    viewerContainer_->installEventFilter(this);
    browserPanel_->raise();

    browserPanel_->setMotionSelectedCallback([this](const std::filesystem::path& cachePath) {
        loadCachePath(cachePath);
    });
    browserPanel_->setImportRequestedCallback([this]() {
        importAmassMotion();
    });
    browserPanel_->setExpansionChangedCallback([this]() {
        updateMotionBrowserGeometry();
        browserPanel_->raise();
    });

    statusBar()->showMessage("Ready");
    refreshMotionList();
    updateMotionBrowserGeometry();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == viewerContainer_ && event->type() == QEvent::Resize) {
        updateMotionBrowserGeometry();
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updateMotionBrowserGeometry();
}

void MainWindow::refreshMotionList()
{
    motions_ = scanMotionCaches(projectRoot_);
    browserPanel_->setMotions(motions_);
}

void MainWindow::loadCachePath(const std::filesystem::path& cachePath)
{
    if (!viewerWidget_->loadMotionCache(cachePath)) {
        QMessageBox::warning(
            this,
            "Load Failed",
            "Failed to load cache:\n" + toQString(cachePath)
        );
        return;
    }

    browserPanel_->setCurrentCache(cachePath);
    statusBar()->showMessage("Loaded " + toQString(cachePath.filename()), 3000);
}

void MainWindow::importAmassMotion()
{
    const std::filesystem::path defaultDir = projectRoot_ / "data" / "amass";
    const QString selectedFile = QFileDialog::getOpenFileName(
        this,
        "Select AMASS Motion",
        toQString(defaultDir),
        "AMASS Motion (*.npz)"
    );

    if (selectedFile.isEmpty()) {
        return;
    }

    const std::filesystem::path motionPath = selectedFile.toStdWString();
    const std::filesystem::path cachePath = makeCachePathForMotion(projectRoot_, motionPath);

    if (!std::filesystem::exists(cachePath)) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        statusBar()->showMessage("Converting " + toQString(motionPath.filename()) + "...");
        const ConverterResult result = runConverter(projectRoot_, motionPath, cachePath);
        QApplication::restoreOverrideCursor();

        if (!result.ok) {
            showConversionFailure(this, motionPath, result);
            statusBar()->showMessage("Conversion failed", 3000);
            return;
        }
    }

    refreshMotionList();
    loadCachePath(cachePath);
}

void MainWindow::updateMotionBrowserGeometry()
{
    if (!viewerContainer_ || !viewerWidget_ || !browserPanel_) {
        return;
    }

    const QSize containerSize = viewerContainer_->size();
    viewerWidget_->setGeometry(QRect(QPoint(0, 0), containerSize));

    constexpr int margin = 12;
    constexpr int expandedWidth = 340;
    constexpr int minExpandedHeight = 180;
    constexpr int maxExpandedHeight = 280;

    const QSize collapsedSize = browserPanel_->sizeHint();
    const int availableWidth = std::max(0, containerSize.width() - margin * 2);
    const int availableHeight = std::max(0, containerSize.height() - margin * 2);

    int overlayWidth = std::min(collapsedSize.width(), availableWidth);
    int overlayHeight = std::min(collapsedSize.height(), availableHeight);

    if (browserPanel_->isExpanded()) {
        overlayWidth = std::min(expandedWidth, availableWidth);

        const int targetHeight = containerSize.height() / 4;
        const int expandedMaxHeight = std::min(maxExpandedHeight, availableHeight);
        const int expandedHeight = clampInt(
            targetHeight,
            std::min(minExpandedHeight, expandedMaxHeight),
            expandedMaxHeight
        );
        overlayHeight = expandedHeight;
    }

    browserPanel_->setGeometry(margin, margin, overlayWidth, overlayHeight);
    browserPanel_->raise();
}
