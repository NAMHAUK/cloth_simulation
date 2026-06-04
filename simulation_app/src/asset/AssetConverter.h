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

class AssetConverter final : public QObject {
public:
    using ConversionSucceededCallback = std::function<void()>;
    using ConversionFailedCallback = std::function<void(const std::string&)>;

    explicit AssetConverter(QObject* parent = nullptr);
    ~AssetConverter() override;

    AssetConverter(const AssetConverter&) = delete;
    AssetConverter& operator=(const AssetConverter&) = delete;

    bool is_running() const;

    void set_conversion_succeeded_callback(ConversionSucceededCallback callback);
    void set_conversion_failed_callback(ConversionFailedCallback callback);

    void start_conversion(const ConverterCommand& command);

private:
    void setup_process_callbacks();
    void finish_process(int exit_code, QProcess::ExitStatus exit_status);

    QProcess* process_ = nullptr;
    ConverterResult result_;
    ConversionSucceededCallback conversion_succeeded_callback_;
    ConversionFailedCallback conversion_failed_callback_;
};
