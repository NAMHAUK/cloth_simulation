#pragma once

#include <string>

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

struct ConverterResult
{
    bool succeeded = false;
    int exit_code = -1;
    std::string error_message;
};

struct ConverterCommand
{
    bool is_valid = false;
    QString program;
    QStringList arguments;
    QString working_directory;
    std::string error_message;
};

class AssetConverter final : public QObject
{
    Q_OBJECT

public:
    explicit AssetConverter(QObject* parent = nullptr);
    ~AssetConverter() override;

    AssetConverter(const AssetConverter&) = delete;
    AssetConverter& operator=(const AssetConverter&) = delete;

    bool is_running() const;

    void start_conversion(const ConverterCommand& command);

Q_SIGNALS:
    void conversion_succeeded();
    void conversion_failed(const std::string& error_message);

private:
    void connect_process();
    void finish_process(int exit_code, QProcess::ExitStatus exit_status);

    QProcess* process_ = nullptr;
    ConverterResult result_;
};
