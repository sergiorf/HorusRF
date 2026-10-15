// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QMainWindow>
#include <QString>

class QCloseEvent;
class QAction;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;

namespace horusrf::studio {

class DslEditor;
class ExperimentRunner;
class ResultsView;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr,
                        const QString& runnerExecutable = {});

    DslEditor* editor() const noexcept { return editor_; }
    ExperimentRunner* runner() const noexcept { return runner_; }
    QString currentFilePath() const { return current_file_path_; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createActions();
    void createLayout();
    void newDocument();
    void openDocument();
    bool saveDocument();
    bool saveDocumentAs();
    bool writeDocument(const QString& path);
    bool loadDocument(const QString& path);
    bool confirmSaveIfModified();
    void setCurrentFile(const QString& path);
    void updateWindowTitle();
    void validateExperiment();
    void runExperiment();
    void experimentCompleted();
    void navigateToDiagnostic(QListWidgetItem* item);

    DslEditor* editor_ = nullptr;
    ExperimentRunner* runner_ = nullptr;
    ResultsView* results_ = nullptr;
    QListWidget* diagnostics_ = nullptr;
    QPlainTextEdit* standard_output_ = nullptr;
    QPlainTextEdit* standard_error_ = nullptr;
    QAction* validate_action_ = nullptr;
    QAction* run_action_ = nullptr;
    QString current_file_path_;
};

} // namespace horusrf::studio
