// SPDX-License-Identifier: GPL-3.0-only
#include "assistant_panel.hpp"

#include "dsl_editor.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

#include <chrono>
#include <string>
#include <variant>

namespace horusrf::studio {
namespace {

QString fromUtf8(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString lineDiff(const QString& original, const QString& proposal) {
    const QStringList before = original.split(QLatin1Char('\n'));
    const QStringList after = proposal.split(QLatin1Char('\n'));
    qsizetype prefix{};
    while (prefix < before.size() && prefix < after.size() &&
           before[prefix] == after[prefix]) ++prefix;
    qsizetype suffix{};
    while (suffix < before.size() - prefix && suffix < after.size() - prefix &&
           before[before.size() - suffix - 1] == after[after.size() - suffix - 1]) {
        ++suffix;
    }
    QStringList output;
    for (qsizetype index = 0; index < prefix; ++index)
        output.append(QStringLiteral("  ") + before[index]);
    for (qsizetype index = prefix; index < before.size() - suffix; ++index)
        output.append(QStringLiteral("- ") + before[index]);
    for (qsizetype index = prefix; index < after.size() - suffix; ++index)
        output.append(QStringLiteral("+ ") + after[index]);
    for (qsizetype index = before.size() - suffix; index < before.size(); ++index)
        output.append(QStringLiteral("  ") + before[index]);
    return output.join(QLatin1Char('\n'));
}

QString diagnosticCategory(ai::DiagnosticCategory category) {
    switch (category) {
    case ai::DiagnosticCategory::Syntax: return QStringLiteral("syntax");
    case ai::DiagnosticCategory::Semantic: return QStringLiteral("semantic");
    case ai::DiagnosticCategory::Lowering: return QStringLiteral("lowering");
    }
    return QStringLiteral("validation");
}

} // namespace

AssistantPanel::AssistantPanel(DslEditor* editor, AssistantService* service,
                               QWidget* parent)
    : QWidget(parent), editor_(editor), service_(service) {
    setObjectName(QStringLiteral("assistantPanel"));
    auto* layout = new QVBoxLayout(this);

    auto* configuration = new QGroupBox(tr("Local provider"), this);
    auto* configuration_layout = new QGridLayout(configuration);
    endpoint_ = new QLineEdit(QStringLiteral("http://127.0.0.1:11434"), configuration);
    endpoint_->setObjectName(QStringLiteral("assistantEndpoint"));
    model_ = new QComboBox(configuration);
    model_->setObjectName(QStringLiteral("assistantModel"));
    model_->setEditable(true);
    model_->addItem(QStringLiteral("qwen2.5-coder:7b"));
    timeout_ = new QSpinBox(configuration);
    timeout_->setObjectName(QStringLiteral("assistantTimeout"));
    timeout_->setRange(1, 1800);
    timeout_->setValue(120);
    timeout_->setSuffix(tr(" s"));
    refresh_models_ = new QPushButton(tr("Check models"), configuration);
    refresh_models_->setObjectName(QStringLiteral("assistantRefreshModels"));
    configuration_layout->addWidget(new QLabel(tr("Endpoint:"), configuration), 0, 0);
    configuration_layout->addWidget(endpoint_, 0, 1, 1, 2);
    configuration_layout->addWidget(new QLabel(tr("Model:"), configuration), 1, 0);
    configuration_layout->addWidget(model_, 1, 1);
    configuration_layout->addWidget(refresh_models_, 1, 2);
    configuration_layout->addWidget(new QLabel(tr("Timeout:"), configuration), 2, 0);
    configuration_layout->addWidget(timeout_, 2, 1);
    layout->addWidget(configuration);

    mode_ = new QComboBox(this);
    mode_->setObjectName(QStringLiteral("assistantMode"));
    mode_->addItem(tr("Generate a new script"));
    mode_->addItem(tr("Modify the current script"));
    layout->addWidget(mode_);
    instruction_ = new QPlainTextEdit(this);
    instruction_->setObjectName(QStringLiteral("assistantInstruction"));
    instruction_->setPlaceholderText(tr("Describe the RF experiment or requested change..."));
    instruction_->setMaximumHeight(110);
    layout->addWidget(instruction_);

    auto* request_buttons = new QGridLayout;
    generate_ = new QPushButton(tr("Generate"), this);
    generate_->setObjectName(QStringLiteral("assistantGenerate"));
    cancel_ = new QPushButton(tr("Cancel"), this);
    cancel_->setObjectName(QStringLiteral("assistantCancel"));
    cancel_->setEnabled(false);
    retry_ = new QPushButton(tr("Retry"), this);
    retry_->setObjectName(QStringLiteral("assistantRetry"));
    retry_->setEnabled(false);
    request_buttons->addWidget(generate_, 0, 0);
    request_buttons->addWidget(cancel_, 0, 1);
    request_buttons->addWidget(retry_, 0, 2);
    layout->addLayout(request_buttons);

    status_ = new QLabel(tr("Ready."), this);
    status_->setObjectName(QStringLiteral("assistantStatus"));
    status_->setWordWrap(true);
    layout->addWidget(status_);
    latency_ = new QLabel(tr("Response time: --"), this);
    latency_->setObjectName(QStringLiteral("assistantLatency"));
    layout->addWidget(latency_);
    latency_timer_ = new QTimer(this);
    latency_timer_->setInterval(250);
    connect(latency_timer_, &QTimer::timeout,
            this, &AssistantPanel::updateRequestLatency);
    stale_warning_ = new QLabel(this);
    stale_warning_->setObjectName(QStringLiteral("assistantStaleWarning"));
    stale_warning_->setWordWrap(true);
    stale_warning_->setVisible(false);
    layout->addWidget(stale_warning_);

    auto* previews = new QTabWidget(this);
    previews->setObjectName(QStringLiteral("assistantPreviewTabs"));
    proposal_ = new QPlainTextEdit(previews);
    proposal_->setObjectName(QStringLiteral("assistantProposal"));
    proposal_->setReadOnly(true);
    changes_ = new QPlainTextEdit(previews);
    changes_->setObjectName(QStringLiteral("assistantChanges"));
    changes_->setReadOnly(true);
    details_ = new QPlainTextEdit(previews);
    details_->setObjectName(QStringLiteral("assistantDetails"));
    details_->setReadOnly(true);
    previews->addTab(proposal_, tr("Proposal"));
    previews->addTab(changes_, tr("Changes"));
    previews->addTab(details_, tr("Validation"));
    layout->addWidget(previews, 1);

    auto* proposal_buttons = new QGridLayout;
    apply_ = new QPushButton(tr("Apply to Editor"), this);
    apply_->setObjectName(QStringLiteral("assistantApply"));
    apply_->setEnabled(false);
    discard_ = new QPushButton(tr("Discard"), this);
    discard_->setObjectName(QStringLiteral("assistantDiscard"));
    discard_->setEnabled(false);
    proposal_buttons->addWidget(apply_, 0, 0);
    proposal_buttons->addWidget(discard_, 0, 1);
    layout->addLayout(proposal_buttons);

    connect(generate_, &QPushButton::clicked, this, &AssistantPanel::submit);
    connect(cancel_, &QPushButton::clicked, this, [this] {
        service_->cancel();
        status_->setText(tr("Cancelling..."));
        cancel_->setEnabled(false);
    });
    connect(retry_, &QPushButton::clicked, this, &AssistantPanel::retry);
    connect(apply_, &QPushButton::clicked, this, &AssistantPanel::applyProposal);
    connect(discard_, &QPushButton::clicked, this, &AssistantPanel::discardProposal);
    connect(refresh_models_, &QPushButton::clicked, this, &AssistantPanel::refreshModels);
    connect(service_, &AssistantService::requestFinished,
            this, &AssistantPanel::requestFinished);
    connect(service_, &AssistantService::discoveryFinished,
            this, &AssistantPanel::discoveryFinished);
    connect(editor_->document(), &QTextDocument::contentsChanged,
            this, &AssistantPanel::updateStaleness);
}

AssistantRequest AssistantPanel::currentRequest() const {
    AssistantRequest request;
    request.mode = mode_->currentIndex() == 0 ? AssistantMode::Generate
                                               : AssistantMode::Modify;
    request.instruction = instruction_->toPlainText().toUtf8().toStdString();
    request.current_source = editor_->toPlainText().toUtf8().toStdString();
    request.endpoint = endpoint_->text().trimmed().toUtf8().toStdString();
    request.model = model_->currentText().trimmed().toUtf8().toStdString();
    request.timeout = std::chrono::seconds{timeout_->value()};
    return request;
}

void AssistantPanel::submit() {
    if (instruction_->toPlainText().trimmed().isEmpty()) {
        status_->setText(tr("Enter a request before generating."));
        return;
    }
    if (endpoint_->text().trimmed().isEmpty() || model_->currentText().trimmed().isEmpty()) {
        status_->setText(tr("Configure an Ollama endpoint and model first."));
        return;
    }
    clearProposal();
    request_source_ = editor_->toPlainText();
    request_revision_ = editor_->document()->revision();
    last_instruction_ = instruction_->toPlainText();
    last_mode_ = mode_->currentIndex() == 0 ? AssistantMode::Generate
                                            : AssistantMode::Modify;
    if (service_->startRequest(currentRequest())) {
        startRequestTimer();
        setBusy(true, tr("Generating, validating, and repairing if needed..."));
    }
}

void AssistantPanel::retry() {
    instruction_->setPlainText(last_instruction_);
    mode_->setCurrentIndex(last_mode_ == AssistantMode::Generate ? 0 : 1);
    submit();
}

void AssistantPanel::requestFinished() {
    finishRequestTimer();
    setBusy(false, tr("Request finished."));
    const auto result = service_->lastResult();
    if (!result) {
        status_->setText(tr("The assistant returned no result."));
        return;
    }
    const ai::ScriptProposal* proposal{};
    QStringList detail_lines;
    if (const auto* success = std::get_if<ai::ScriptProposal>(result.get())) {
        proposal = success;
        proposal_valid_ = success->eligible_to_apply();
        status_->setText(tr("Validated proposal from %1 / %2 after %3 attempt(s).")
                             .arg(fromUtf8(success->provider().provider),
                                  fromUtf8(success->provider().model))
                             .arg(success->attempt_count()));
    } else {
        const auto& failure = std::get<ai::AgentFailure>(*result);
        status_->setText(fromUtf8(failure.error.message));
        if (failure.proposal) proposal = &*failure.proposal;
        proposal_valid_ = false;
    }
    if (proposal != nullptr) {
        const QString source = fromUtf8(proposal->candidate_source().text());
        proposal_->setPlainText(source);
        changes_->setPlainText(lineDiff(request_source_, source));
        if (!proposal->explanation().empty()) {
            detail_lines.append(tr("Explanation: %1")
                                    .arg(fromUtf8(proposal->explanation())));
        }
        for (const auto& warning : proposal->warnings()) {
            detail_lines.append(tr("Warning: %1").arg(fromUtf8(warning)));
        }
        for (const auto& diagnostic : proposal->validation().diagnostics()) {
            detail_lines.append(
                tr("%1:%2: %3: %4")
                    .arg(diagnostic.span.begin.line)
                    .arg(diagnostic.span.begin.column)
                    .arg(diagnosticCategory(diagnostic.category) + QLatin1Char('.') +
                             fromUtf8(diagnostic.code),
                         fromUtf8(diagnostic.message)));
        }
        if (detail_lines.isEmpty()) detail_lines.append(tr("Compiler validation succeeded."));
        details_->setPlainText(detail_lines.join(QLatin1Char('\n')));
        discard_->setEnabled(true);
    }
    retry_->setEnabled(true);
    updateStaleness();
}

void AssistantPanel::applyProposal() {
    updateStaleness();
    if (!apply_->isEnabled()) return;
    QTextCursor cursor(editor_->document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(proposal_->toPlainText());
    cursor.endEditBlock();
    status_->setText(tr("Proposal applied. Review, save, and run it explicitly when ready."));
    clearProposal();
    editor_->setFocus();
}

void AssistantPanel::discardProposal() {
    clearProposal();
    status_->setText(tr("Proposal discarded. The editor was not changed."));
}

void AssistantPanel::refreshModels() {
    if (endpoint_->text().trimmed().isEmpty()) {
        status_->setText(tr("Enter an Ollama endpoint first."));
        return;
    }
    if (service_->discoverModels(endpoint_->text().trimmed().toUtf8().toStdString(),
                                 std::chrono::seconds{timeout_->value()})) {
        setBusy(true, tr("Checking Ollama and installed models..."));
    }
}

void AssistantPanel::discoveryFinished() {
    setBusy(false, tr("Provider check finished."));
    const auto result = service_->lastDiscovery();
    if (!result || !result->ok()) {
        const QString error = result ? fromUtf8(result->error) : tr("No response.");
        status_->setText(tr("Ollama is unavailable: %1 Start Ollama, then install a model "
                            "with 'ollama pull qwen2.5-coder:7b'.")
                             .arg(error));
        return;
    }
    const QString selected = model_->currentText();
    model_->clear();
    for (const auto& name : result->models) model_->addItem(fromUtf8(name));
    const int previous = model_->findText(selected);
    if (previous >= 0) model_->setCurrentIndex(previous);
    else if (model_->count() == 0) model_->setEditText(selected);
    status_->setText(result->models.empty()
                         ? tr("Ollama is available, but no models are installed. Run "
                              "'ollama pull qwen2.5-coder:7b'.")
                         : tr("Ollama is available; found %1 installed model(s).")
                               .arg(result->models.size()));
}

void AssistantPanel::updateStaleness() {
    const bool has_proposal = !proposal_->toPlainText().isEmpty();
    const bool stale = has_proposal &&
                       (editor_->document()->revision() != request_revision_ ||
                        editor_->toPlainText() != request_source_);
    stale_warning_->setVisible(stale);
    stale_warning_->setText(
        stale ? tr("The editor changed after this request started. Review the changes "
                   "and retry to create a proposal from the current document.")
              : QString{});
    apply_->setEnabled(has_proposal && proposal_valid_ && !stale && !service_->isRunning());
}

void AssistantPanel::startRequestTimer() {
    request_timer_.start();
    latency_->setText(tr("Waiting for Ollama: 0.0 s elapsed"));
    latency_timer_->start();
}

void AssistantPanel::updateRequestLatency() {
    if (!request_timer_.isValid()) return;
    const double seconds = static_cast<double>(request_timer_.elapsed()) / 1000.0;
    latency_->setText(tr("Waiting for Ollama: %1 s elapsed").arg(seconds, 0, 'f', 1));
}

void AssistantPanel::finishRequestTimer() {
    latency_timer_->stop();
    if (!request_timer_.isValid()) return;
    const double seconds = static_cast<double>(request_timer_.elapsed()) / 1000.0;
    latency_->setText(tr("Response time: %1 s").arg(seconds, 0, 'f', 1));
    request_timer_.invalidate();
}

void AssistantPanel::setBusy(bool busy, const QString& status) {
    generate_->setEnabled(!busy);
    retry_->setEnabled(!busy && !last_instruction_.isEmpty());
    cancel_->setEnabled(busy);
    refresh_models_->setEnabled(!busy);
    endpoint_->setEnabled(!busy);
    model_->setEnabled(!busy);
    timeout_->setEnabled(!busy);
    mode_->setEnabled(!busy);
    instruction_->setEnabled(!busy);
    status_->setText(status);
    updateStaleness();
}

void AssistantPanel::clearProposal() {
    proposal_->clear();
    changes_->clear();
    details_->clear();
    proposal_valid_ = false;
    apply_->setEnabled(false);
    discard_->setEnabled(false);
    stale_warning_->setVisible(false);
}

} // namespace horusrf::studio
