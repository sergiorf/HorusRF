// SPDX-License-Identifier: GPL-3.0-only
#include "main_window.hpp"

#include "dsl_editor.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSaveFile>
#include <QSplitter>
#include <QStringDecoder>
#include <QTextDocument>
#include <QToolBar>

static void initializeStudioResources() {
    Q_INIT_RESOURCE(studio_resources);
}

namespace horusrf::studio {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    initializeStudioResources();
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
}

void MainWindow::createLayout() {
    editor_ = new DslEditor(this);

    auto* results = new QLabel(tr("Results will appear here in a later milestone."));
    results->setObjectName(QStringLiteral("resultsPlaceholder"));
    results->setAlignment(Qt::AlignCenter);

    auto* diagnostics = new QLabel(
        tr("Diagnostics will appear here after validation is implemented."));
    diagnostics->setObjectName(QStringLiteral("diagnosticsPlaceholder"));
    diagnostics->setAlignment(Qt::AlignCenter);

    auto* upper = new QSplitter(Qt::Horizontal, this);
    upper->addWidget(editor_);
    upper->addWidget(results);
    upper->setStretchFactor(0, 3);
    upper->setStretchFactor(1, 2);

    auto* workspace = new QSplitter(Qt::Vertical, this);
    workspace->addWidget(upper);
    workspace->addWidget(diagnostics);
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
        event->accept();
    } else {
        event->ignore();
    }
}

} // namespace horusrf::studio
