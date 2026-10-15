// SPDX-License-Identifier: GPL-3.0-only
#include "main_window.hpp"

#include "dsl_editor.hpp"
#include "experiment_runner.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QSaveFile>
#include <QSplitter>
#include <QStringDecoder>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolBar>

static void initializeStudioResources() {
    Q_INIT_RESOURCE(studio_resources);
}

namespace horusrf::studio {

MainWindow::MainWindow(QWidget* parent, const QString& runnerExecutable)
    : QMainWindow(parent) {
    initializeStudioResources();
    runner_ = new ExperimentRunner(runnerExecutable, this);
    createLayout();
    createActions();

    QFile example(QStringLiteral(":/examples/tx_path_characterization.hrf"));
    if (example.open(QIODevice::ReadOnly)) {
        editor_->setPlainText(QString::fromUtf8(example.readAll()));
    }
    editor_->document()->setModified(false);
    setCurrentFile({});
    resize(1100, 720);

    connect(editor_->document(), &QTextDocument::modificationChanged, this,
            [this](bool modified) {
                setWindowModified(modified);
                updateWindowTitle();
            });
    connect(runner_, &ExperimentRunner::completed, this,
            &MainWindow::experimentCompleted);
}

void MainWindow::createLayout() {
    editor_ = new DslEditor(this);

    results_ = new QLabel(tr("Run an experiment to see its summary."));
    results_->setObjectName(QStringLiteral("resultsPlaceholder"));
    results_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    results_->setWordWrap(true);
    results_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    diagnostics_ = new QListWidget(this);
    diagnostics_->setObjectName(QStringLiteral("diagnosticsList"));
    diagnostics_->addItem(tr("No diagnostics."));
    connect(diagnostics_, &QListWidget::itemClicked, this,
            &MainWindow::navigateToDiagnostic);

    standard_output_ = new QPlainTextEdit(this);
    standard_output_->setObjectName(QStringLiteral("standardOutput"));
    standard_output_->setReadOnly(true);
    standard_error_ = new QPlainTextEdit(this);
    standard_error_->setObjectName(QStringLiteral("standardError"));
    standard_error_->setReadOnly(true);

    auto* processOutput = new QTabWidget(this);
    processOutput->setObjectName(QStringLiteral("processOutputTabs"));
    processOutput->addTab(diagnostics_, tr("Diagnostics"));
    processOutput->addTab(standard_output_, tr("Standard output"));
    processOutput->addTab(standard_error_, tr("Standard error"));

    auto* upper = new QSplitter(Qt::Horizontal, this);
    upper->addWidget(editor_);
    upper->addWidget(results_);
    upper->setStretchFactor(0, 3);
    upper->setStretchFactor(1, 2);

    auto* workspace = new QSplitter(Qt::Vertical, this);
    workspace->addWidget(upper);
    workspace->addWidget(processOutput);
    workspace->setStretchFactor(0, 4);
    workspace->setStretchFactor(1, 1);
    setCentralWidget(workspace);
}

void MainWindow::createActions() {
    auto* file = menuBar()->addMenu(tr("&File"));
    auto* edit = menuBar()->addMenu(tr("&Edit"));
    auto* toolbar = addToolBar(tr("File"));
    toolbar->setObjectName(QStringLiteral("fileToolBar"));

    auto addFileAction = [this, file, toolbar](const QString& text,
                                               const QKeySequence& shortcut,
                                               auto handler) {
        auto* action = new QAction(text, this);
        action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, handler);
        file->addAction(action);
        toolbar->addAction(action);
        return action;
    };

    addFileAction(tr("&New"), QKeySequence::New, [this] { newDocument(); });
    addFileAction(tr("&Open..."), QKeySequence::Open, [this] { openDocument(); });
    addFileAction(tr("&Save"), QKeySequence::Save,
                  [this] { static_cast<void>(saveDocument()); });
    addFileAction(tr("Save &As..."), QKeySequence::SaveAs,
                  [this] { static_cast<void>(saveDocumentAs()); });
    file->addSeparator();
    auto* exit = file->addAction(tr("E&xit"));
    exit->setShortcut(QKeySequence::Quit);
    connect(exit, &QAction::triggered, this, &QWidget::close);

    auto* undo = edit->addAction(tr("&Undo"));
    undo->setShortcut(QKeySequence::Undo);
    undo->setEnabled(false);
    connect(undo, &QAction::triggered, editor_, &QPlainTextEdit::undo);
    connect(editor_, &QPlainTextEdit::undoAvailable, undo, &QAction::setEnabled);

    auto* redo = edit->addAction(tr("&Redo"));
    redo->setShortcut(QKeySequence::Redo);
    redo->setEnabled(false);
    connect(redo, &QAction::triggered, editor_, &QPlainTextEdit::redo);
    connect(editor_, &QPlainTextEdit::redoAvailable, redo, &QAction::setEnabled);

    auto* experiment = menuBar()->addMenu(tr("&Experiment"));
    auto* experimentToolbar = addToolBar(tr("Experiment"));
    experimentToolbar->setObjectName(QStringLiteral("experimentToolBar"));

    validate_action_ = experiment->addAction(tr("&Validate"));
    validate_action_->setObjectName(QStringLiteral("validateAction"));
    validate_action_->setShortcut(QKeySequence(Qt::Key_F5));
    connect(validate_action_, &QAction::triggered, this,
            &MainWindow::validateExperiment);
    experimentToolbar->addAction(validate_action_);

    run_action_ = experiment->addAction(tr("&Run"));
    run_action_->setObjectName(QStringLiteral("runAction"));
    run_action_->setShortcut(QKeySequence(Qt::Key_F6));
    connect(run_action_, &QAction::triggered, this, &MainWindow::runExperiment);
    experimentToolbar->addAction(run_action_);
}

void MainWindow::validateExperiment() {
    results_->setText(tr("Validating..."));
    diagnostics_->clear();
    standard_output_->clear();
    standard_error_->clear();
    validate_action_->setEnabled(false);
    run_action_->setEnabled(false);
    if (!runner_->validate(editor_->toPlainText())) {
        validate_action_->setEnabled(true);
        run_action_->setEnabled(true);
    }
}

void MainWindow::runExperiment() {
    results_->setText(tr("Running..."));
    diagnostics_->clear();
    standard_output_->clear();
    standard_error_->clear();
    validate_action_->setEnabled(false);
    run_action_->setEnabled(false);
    if (!runner_->run(editor_->toPlainText())) {
        validate_action_->setEnabled(true);
        run_action_->setEnabled(true);
    }
}

void MainWindow::experimentCompleted() {
    validate_action_->setEnabled(true);
    run_action_->setEnabled(true);

    const auto& result = runner_->lastResult();
    standard_output_->setPlainText(result.standard_output);
    standard_error_->setPlainText(result.standard_error);
    diagnostics_->clear();
    for (const auto& diagnostic : result.diagnostics) {
        auto* item = new QListWidgetItem(
            tr("%1:%2: %3: %4")
                .arg(diagnostic.line)
                .arg(diagnostic.column)
                .arg(diagnostic.code, diagnostic.message),
            diagnostics_);
        item->setData(Qt::UserRole, diagnostic.line);
        item->setData(Qt::UserRole + 1, diagnostic.column);
        item->setToolTip(diagnostic.path);
    }
    if (result.diagnostics.isEmpty()) diagnostics_->addItem(tr("No diagnostics."));

    if (result.succeeded()) {
        if (result.operation == ExperimentRunner::Operation::Validate) {
            results_->setText(tr("Validation succeeded."));
        } else {
            const QString summary = result.standard_output.trimmed();
            results_->setText(summary.isEmpty() ? tr("Experiment completed.") : summary);
        }
    } else if (result.exit_status == QProcess::CrashExit) {
        results_->setText(tr("The HorusRF runner terminated abnormally."));
    } else if (result.exit_code < 0) {
        results_->setText(tr("The HorusRF runner could not be started."));
    } else {
        results_->setText(tr("The HorusRF runner failed with exit code %1.")
                              .arg(result.exit_code));
    }
}

void MainWindow::navigateToDiagnostic(QListWidgetItem* item) {
    const int line = item->data(Qt::UserRole).toInt();
    const int column = item->data(Qt::UserRole + 1).toInt();
    if (line < 1 || column < 1) return;

    const QTextBlock block = editor_->document()->findBlockByNumber(line - 1);
    if (!block.isValid()) return;
    QTextCursor cursor(block);
    const int offset = qMin(column - 1, qMax(0, block.length() - 1));
    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, offset);
    editor_->setTextCursor(cursor);
    editor_->setFocus();
    editor_->centerCursor();
}

void MainWindow::newDocument() {
    if (!confirmSaveIfModified()) return;
    editor_->clear();
    editor_->document()->setModified(false);
    setCurrentFile({});
}

void MainWindow::openDocument() {
    if (!confirmSaveIfModified()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open HorusRF Source"), {}, tr("HorusRF source (*.hrf);;All files (*)"));
    if (!path.isEmpty()) loadDocument(path);
}

bool MainWindow::loadDocument(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Open failed"),
                              tr("Could not read %1:\n%2").arg(path, file.errorString()));
        return false;
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder.decode(file.readAll());
    if (decoder.hasError()) {
        QMessageBox::critical(this, tr("Open failed"),
                              tr("%1 is not valid UTF-8.").arg(path));
        return false;
    }
    editor_->setPlainText(text);
    editor_->document()->setModified(false);
    setCurrentFile(path);
    return true;
}

bool MainWindow::saveDocument() {
    return current_file_path_.isEmpty() ? saveDocumentAs()
                                         : writeDocument(current_file_path_);
}

bool MainWindow::saveDocumentAs() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save HorusRF Source"), current_file_path_,
        tr("HorusRF source (*.hrf);;All files (*)"));
    return !path.isEmpty() && writeDocument(path);
}

bool MainWindow::writeDocument(const QString& path) {
    QSaveFile file(path);
    const QByteArray contents = editor_->toPlainText().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() ||
        !file.commit()) {
        QMessageBox::critical(this, tr("Save failed"),
                              tr("Could not write %1:\n%2").arg(path, file.errorString()));
        return false;
    }
    editor_->document()->setModified(false);
    setCurrentFile(path);
    return true;
}

bool MainWindow::confirmSaveIfModified() {
    if (!editor_->document()->isModified()) return true;
    const auto choice = QMessageBox::warning(
        this, tr("Unsaved changes"), tr("The document has unsaved changes."),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice == QMessageBox::Save) return saveDocument();
    return choice == QMessageBox::Discard;
}

void MainWindow::setCurrentFile(const QString& path) {
    current_file_path_ = path.isEmpty() ? QString{} : QFileInfo(path).absoluteFilePath();
    updateWindowTitle();
}

void MainWindow::updateWindowTitle() {
    const QString name = current_file_path_.isEmpty()
                             ? tr("Untitled")
                             : QFileInfo(current_file_path_).fileName();
    setWindowTitle(tr("%1[*] - HorusRF Studio").arg(name));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (confirmSaveIfModified()) {
        runner_->cancel();
        event->accept();
    } else {
        event->ignore();
    }
}

} // namespace horusrf::studio
