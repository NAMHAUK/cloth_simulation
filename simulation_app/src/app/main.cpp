#include "ui/MainWindow.h"

#include <filesystem>

#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char* argv[])
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);

    const std::filesystem::path project_root = std::filesystem::path(PROJECT_ROOT_DIR);
    MainWindow window(project_root);
    window.show();

    return app.exec();
}
