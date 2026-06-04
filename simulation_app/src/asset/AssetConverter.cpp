#include "asset/AssetConverter.h"

namespace asset_converter {
bool start_conversion(QObject* owner,
                      QProcess*& process,
                      ConverterResult& result,
                      const ConverterCommand& command,
                      const SetupProcessCallbacks& setup_process_callbacks)
{
    if (process) {
        return false;
    }

    result = {};
    process = new QProcess(owner);
    process->setProgram(command.program);
    process->setArguments(command.arguments);
    process->setWorkingDirectory(command.working_directory);

    setup_process_callbacks();
    process->start();
    return true;
}
}
