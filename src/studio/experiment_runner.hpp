// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QVector>

#include <memory>

class QTemporaryDir;

namespace horusrf::studio {

struct SourceDiagnostic {
    QString path;
    int line = 0;
    int column = 0;
    QString code;
    QString message;
};

class ExperimentRunner final : public QObject {
    Q_OBJECT

public:
    enum class Operation { Validate, Run };

    struct Result {
        Operation operation = Operation::Validate;
        int exit_code = -1;
        QProcess::ExitStatus exit_status = QProcess::NormalExit;
        QString standard_output;
        QString standard_error;
        QVector<SourceDiagnostic> diagnostics;
        QString csv_path;

        [[nodiscard]] bool succeeded() const noexcept {
            return exit_status == QProcess::NormalExit && exit_code == 0;
        }
    };

    explicit ExperimentRunner(QString executablePath = {}, QObject* parent = nullptr);
    ~ExperimentRunner() override;

    [[nodiscard]] bool validate(const QString& source);
    [[nodiscard]] bool run(const QString& source);
    [[nodiscard]] bool isRunning() const noexcept { return active_; }
    [[nodiscard]] const Result& lastResult() const noexcept { return last_result_; }
    [[nodiscard]] QString executablePath() const { return executable_path_; }

    void cancel();

    [[nodiscard]] static QVector<SourceDiagnostic> parseDiagnostics(
        const QString& standardError);

signals:
    void started();
    void completed();

private:
    bool start(Operation operation, const QString& source);
    void finish(int exitCode, QProcess::ExitStatus exitStatus,
                const QString& processError = {});

    QString executable_path_;
    QProcess process_;
    std::unique_ptr<QTemporaryDir> temporary_directory_;
    QByteArray standard_output_;
    QByteArray standard_error_;
    Result last_result_;
    Operation operation_ = Operation::Validate;
    bool active_ = false;
};

} // namespace horusrf::studio
