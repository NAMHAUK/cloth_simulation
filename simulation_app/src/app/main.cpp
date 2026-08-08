#include "ui/MainWindow.h"

#include <filesystem>

#include <QApplication>
#include <QFile>
#include <QSurfaceFormat>

int main(int argc, char* argv[])
{
    QSurfaceFormat format;
    format.setVersion(4, 6);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);

    QFile style_file(":/styles/app.qss");
    if (style_file.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(style_file.readAll());
    }

    const std::filesystem::path project_root = std::filesystem::path(PROJECT_ROOT_DIR);
    MainWindow window(project_root);
    window.show();

    return app.exec();
}
