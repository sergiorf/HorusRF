#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace horusrf::ai {

class ScriptSource final {
public:
    explicit ScriptSource(std::string text) : text_(std::move(text)) {}

    [[nodiscard]] const std::string& text() const noexcept { return text_; }

    friend bool operator==(const ScriptSource&, const ScriptSource&) = default;

private:
    std::string text_;
};

class NaturalLanguageInstruction final {
public:
    explicit NaturalLanguageInstruction(std::string text) : text_(std::move(text)) {}

    [[nodiscard]] const std::string& text() const noexcept { return text_; }

    friend bool operator==(const NaturalLanguageInstruction&,
                           const NaturalLanguageInstruction&) = default;

private:
    std::string text_;
};

struct GenerationRequest final {
    NaturalLanguageInstruction instruction;
};

struct ModificationRequest final {
    ScriptSource current_source;
    NaturalLanguageInstruction instruction;
};

struct ProviderIdentity final {
    std::string provider;
    std::string model;

    friend bool operator==(const ProviderIdentity&, const ProviderIdentity&) = default;
};

// Provider-independent inference telemetry. Providers leave fields empty when
// the backend does not report them; callers must not substitute estimates.
struct ProviderMetrics final {
    std::optional<std::chrono::nanoseconds> model_load_duration;
    std::optional<std::chrono::nanoseconds> prompt_evaluation_duration;
    std::optional<std::chrono::nanoseconds> generation_duration;
    std::optional<std::size_t> input_token_count;
    std::optional<std::size_t> output_token_count;

    friend bool operator==(const ProviderMetrics&, const ProviderMetrics&) = default;
};

struct ProviderResponse final {
    std::string text;
    ProviderIdentity identity;
    std::optional<ProviderMetrics> metrics;
};

enum class DiagnosticCategory { Syntax, Semantic, Lowering };

struct SourcePosition final {
    std::size_t line{};
    std::size_t column{};
    std::size_t offset{};

    friend bool operator==(const SourcePosition&, const SourcePosition&) = default;
};

struct SourceSpan final {
    SourcePosition begin;
    SourcePosition end;

    friend bool operator==(const SourceSpan&, const SourceSpan&) = default;
};

struct ValidationDiagnostic final {
    DiagnosticCategory category{DiagnosticCategory::Syntax};
    std::string code;
    std::string message;
    SourceSpan span;

    friend bool operator==(const ValidationDiagnostic&,
                           const ValidationDiagnostic&) = default;
};

class ValidationResult final {
public:
    [[nodiscard]] static ValidationResult valid();
    [[nodiscard]] static ValidationResult invalid(
        std::vector<ValidationDiagnostic> diagnostics);

    [[nodiscard]] bool ok() const noexcept { return valid_; }
    [[nodiscard]] const std::vector<ValidationDiagnostic>& diagnostics() const noexcept {
        return diagnostics_;
    }

private:
    ValidationResult(bool valid, std::vector<ValidationDiagnostic> diagnostics);

    bool valid_{};
    std::vector<ValidationDiagnostic> diagnostics_;
};

class RepairAttempt final {
public:
    RepairAttempt(std::size_t number, ScriptSource candidate_source,
                  ValidationResult validation, ProviderIdentity provider);

    [[nodiscard]] std::size_t number() const noexcept { return number_; }
    [[nodiscard]] const ScriptSource& candidate_source() const noexcept {
        return candidate_source_;
    }
    [[nodiscard]] const ValidationResult& validation() const noexcept {
        return validation_;
    }
    [[nodiscard]] const ProviderIdentity& provider() const noexcept { return provider_; }

private:
    std::size_t number_{};
    ScriptSource candidate_source_;
    ValidationResult validation_;
    ProviderIdentity provider_;
};

class ScriptProposal final {
public:
    ScriptProposal(ScriptSource candidate_source, ValidationResult validation,
                   std::string explanation, std::size_t attempt_count,
                   ProviderIdentity provider, std::vector<std::string> warnings = {});

    [[nodiscard]] const ScriptSource& candidate_source() const noexcept {
        return candidate_source_;
    }
    [[nodiscard]] const ValidationResult& validation() const noexcept {
        return validation_;
    }
    [[nodiscard]] const std::string& explanation() const noexcept {
        return explanation_;
    }
    [[nodiscard]] std::size_t attempt_count() const noexcept { return attempt_count_; }
    [[nodiscard]] const ProviderIdentity& provider() const noexcept { return provider_; }
    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept {
        return warnings_;
    }
    [[nodiscard]] bool eligible_to_apply() const noexcept { return validation_.ok(); }

private:
    const ScriptSource candidate_source_;
    const ValidationResult validation_;
    const std::string explanation_;
    const std::size_t attempt_count_;
    const ProviderIdentity provider_;
    const std::vector<std::string> warnings_;
};

enum class AgentErrorCode {
    InvalidRequest,
    ProviderFailure,
    ValidationFailure,
    RepairLimitReached,
    Cancelled,
    Internal,
};

struct AgentError final {
    AgentErrorCode code{AgentErrorCode::Internal};
    std::string message;
};

struct AgentFailure final {
    AgentError error;
    std::optional<ScriptProposal> proposal;
};

using AgentResult = std::variant<ScriptProposal, AgentFailure>;

using CancellationToken = std::stop_token;

} // namespace horusrf::ai
