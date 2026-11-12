#include "simulation/SimulationParams.h"
#include "ui/MainWindow.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QSurfaceFormat>

namespace {
SimulationParams parse_simulation_params(const QApplication& app)
{
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"mode", "Simulation mode: pbd or pd.", "mode", "pbd"});
    parser.process(app);

    SimulationParams params = default_simulation_params;
    const QString mode_value = parser.value("mode");
    if (mode_value == "pd") {
        params.mode = SimulationMode::PD;
    } else if (mode_value == "pbd") {
        params.mode = SimulationMode::PBD;
    } else {
        std::fprintf(stderr, "Invalid mode. Use --mode pbd or --mode pd.\n");
        std::exit(1);
    }

    return params;
}
}

int main(int argc, char* argv[])
{
    QSurfaceFormat format;
    format.setVersion(4, 6);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    const SimulationParams params = parse_simulation_params(app);

    QFile style_file(":/styles/app.qss");
    if (style_file.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(style_file.readAll());
    }

    const std::filesystem::path project_root = std::filesystem::path(PROJECT_ROOT_DIR);
    MainWindow window(project_root, params);
    window.show();

    return app.exec();
}
