#pragma once

#include <functional>
#include <string>

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

struct ConverterResult {
    bool succeeded = false;
    int exit_code = -1;
    std::string error_message;
};

struct ConverterCommand {
    bool is_valid = false;
    QString program;
    QStringList arguments;
    QString working_directory;
    std::string error_message;
};

namespace asset_converter {
using SetupProcessCallbacks = std::function<void()>;

bool start_conversion(QObject* owner,
                      QProcess*& process,
                      ConverterResult& result,
                      const ConverterCommand& command,
                      const SetupProcessCallbacks& setup_process_callbacks);
}
