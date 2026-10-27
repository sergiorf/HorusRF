// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "assistant_service.hpp"

#include <QElapsedTimer>
#include <QWidget>

#include <memory>
#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTimer;

namespace horusrf::studio {

class DslEditor;

class AssistantPanel final : public QWidget {
    Q_OBJECT

public:
    AssistantPanel(DslEditor* editor, AssistantService* service,
                   QWidget* parent = nullptr);

private:
    void submit();
    void retry();
    void applyProposal();
    void discardProposal();
    void refreshModels();
    void requestFinished();
    void discoveryFinished();
    void updateStaleness();
    void startRequestTimer(std::chrono::milliseconds timeout);
    void updateRequestLatency();
    void finishRequestTimer(std::optional<std::size_t> attemptCount,
                            bool completed);
    void setBusy(bool busy, const QString& status);
    void clearProposal();
    void restoreSelectedModel();
    void persistSelectedModel() const;
    [[nodiscard]] QString selectedModelIdentifier() const;
    void setConfiguredModel(const QString& identifier);
    [[nodiscard]] AssistantRequest currentRequest() const;

    DslEditor* editor_;
    AssistantService* service_;
    QLineEdit* endpoint_;
    QComboBox* model_;
    QSpinBox* timeout_;
    QComboBox* mode_;
    QPlainTextEdit* instruction_;
    QPushButton* generate_;
    QPushButton* cancel_;
    QPushButton* retry_;
    QPushButton* apply_;
    QPushButton* discard_;
    QPushButton* refresh_models_;
    QLabel* status_;
    QLabel* latency_;
    QProgressBar* request_progress_;
    QLabel* stale_warning_;
    QTabWidget* previews_;
    QTimer* latency_timer_;
    QPlainTextEdit* proposal_;
    QPlainTextEdit* changes_;
    QPlainTextEdit* details_;
    QString request_source_;
    QString last_instruction_;
    AssistantMode last_mode_{AssistantMode::Generate};
    int request_revision_{};
    bool proposal_valid_{};
    QElapsedTimer request_timer_;
    std::chrono::milliseconds request_timeout_{};
};

} // namespace horusrf::studio
