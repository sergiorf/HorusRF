// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QMainWindow>
#include <QString>

class QCloseEvent;

namespace horusrf::studio {

class DslEditor;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);

    DslEditor* editor() const noexcept { return editor_; }
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

    DslEditor* editor_ = nullptr;
    QString current_file_path_;
};

} // namespace horusrf::studio
