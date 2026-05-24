#pragma once

#include "io/MotionAsset.h"

#include <functional>
#include <string>

#include <QObject>
#include <QProcess>

class MotionConverter final : public QObject {
public:
    using ConversionSucceededCallback = std::function<void()>;
    using ConversionFailedCallback = std::function<void(const std::string&)>;

    explicit MotionConverter(QObject* parent = nullptr);
    ~MotionConverter() override;

    MotionConverter(const MotionConverter&) = delete;
    MotionConverter& operator=(const MotionConverter&) = delete;

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
