// SPDX-License-Identifier: GPL-3.0-only
#include "experiment_runner.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <utility>

namespace horusrf::studio {
namespace {

QString defaultExecutablePath() {
#ifdef Q_OS_WIN
    constexpr auto executable = "horusrf-run.exe";
#else
    constexpr auto executable = "horusrf-run";
#endif
    return QDir{QCoreApplication::applicationDirPath()}.filePath(
        QString::fromLatin1(executable));
}

} // namespace

ExperimentRunner::ExperimentRunner(QString executablePath, QObject* parent)
    : QObject(parent),
      executable_path_(executablePath.isEmpty() ? defaultExecutablePath()
                                                 : std::move(executablePath)) {
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
        standard_output_.append(process_.readAllStandardOutput());
    });
    connect(&process_, &QProcess::readyReadStandardError, this, [this] {
        standard_error_.append(process_.readAllStandardError());
    });
    connect(&process_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                if (active_) finish(exitCode, exitStatus);
            });
    connect(&process_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (active_ && error == QProcess::FailedToStart) {
                    finish(-1, QProcess::NormalExit, process_.errorString());
                }
            });
}

ExperimentRunner::~ExperimentRunner() { cancel(); }

bool ExperimentRunner::validate(const QString& source) {
    return start(Operation::Validate, source);
}

bool ExperimentRunner::run(const QString& source) {
    return start(Operation::Run, source);
}

bool ExperimentRunner::start(Operation operation, const QString& source) {
    if (active_) return false;

    temporary_directory_ = std::make_unique<QTemporaryDir>();
    operation_ = operation;
    last_result_ = Result{};
    last_result_.operation = operation;
    standard_output_.clear();
    standard_error_.clear();

    if (!temporary_directory_->isValid()) {
        last_result_.standard_error = tr("Could not create a temporary directory.");
        emit completed();
        return false;
    }

    const QString sourcePath = temporary_directory_->filePath(
        QStringLiteral("current-buffer.hrf"));
    QFile sourceFile(sourcePath);
    const QByteArray encoded = source.toUtf8();
    if (!sourceFile.open(QIODevice::WriteOnly) ||
        sourceFile.write(encoded) != encoded.size() || !sourceFile.flush()) {
        last_result_.standard_error =
            tr("Could not write the temporary source file: %1")
                .arg(sourceFile.errorString());
        emit completed();
        return false;
    }
    sourceFile.close();

    QStringList arguments{sourcePath};
    if (operation == Operation::Validate) {
        arguments.append(QStringLiteral("--check"));
    } else {
        last_result_.json_path = temporary_directory_->filePath(
            QStringLiteral("results.json"));
        arguments.append({QStringLiteral("--json"), last_result_.json_path});
    }

    process_.setProgram(executable_path_);
    process_.setArguments(arguments);
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    active_ = true;
    emit started();
    process_.start();
    return true;
}

void ExperimentRunner::finish(int exitCode, QProcess::ExitStatus exitStatus,
                              const QString& processError) {
    standard_output_.append(process_.readAllStandardOutput());
    standard_error_.append(process_.readAllStandardError());
    if (!processError.isEmpty()) {
        if (!standard_error_.isEmpty() && !standard_error_.endsWith('\n')) {
            standard_error_.append('\n');
        }
        standard_error_.append(processError.toUtf8());
    }

    last_result_.operation = operation_;
    last_result_.exit_code = exitCode;
    last_result_.exit_status = exitStatus;
    last_result_.standard_output = QString::fromUtf8(standard_output_);
    last_result_.standard_error = QString::fromUtf8(standard_error_);
    last_result_.diagnostics = parseDiagnostics(last_result_.standard_error);
    active_ = false;
    emit completed();
}

void ExperimentRunner::cancel() {
    if (!active_) return;
    active_ = false;
    process_.kill();
    process_.waitForFinished(1000);
}

QVector<SourceDiagnostic> ExperimentRunner::parseDiagnostics(
    const QString& standardError) {
    static const QRegularExpression pattern(
        QStringLiteral(R"(^(.+):(\d+):(\d+): ([a-z][a-z0-9_.]*): (.*)$)"));

    QVector<SourceDiagnostic> diagnostics;
    const auto lines = standardError.split(QRegularExpression(QStringLiteral("\\r?\\n")),
                                           Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const auto match = pattern.match(line);
        if (!match.hasMatch()) continue;
        bool validLine = false;
        bool validColumn = false;
        const int lineNumber = match.captured(2).toInt(&validLine);
        const int columnNumber = match.captured(3).toInt(&validColumn);
        if (!validLine || !validColumn || lineNumber < 1 || columnNumber < 1) continue;
        diagnostics.append(SourceDiagnostic{match.captured(1), lineNumber, columnNumber,
                                            match.captured(4), match.captured(5)});
    }
    return diagnostics;
}

} // namespace horusrf::studio
