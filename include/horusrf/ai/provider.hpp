#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "horusrf/ai/contracts.hpp"

namespace horusrf::ai {

enum class LlmRole { System, User, Assistant };
enum class LlmCallPurpose { Generation, CompilerRepair, RequirementCorrection };

struct LlmMessage final {
    LlmRole role{LlmRole::User};
    std::string content;

    friend bool operator==(const LlmMessage&, const LlmMessage&) = default;
};

struct LlmRequest final {
    std::vector<LlmMessage> messages;
    std::string model;
    double temperature{0.0};
    std::size_t max_output_tokens{2048};
    std::chrono::milliseconds timeout{std::chrono::minutes{2}};
    std::optional<std::string> json_schema;
    LlmCallPurpose purpose{LlmCallPurpose::Generation};
};

enum class ProviderErrorCode {
    InvalidRequest,
    InvalidEndpoint,
    Unavailable,
    ModelUnavailable,
    Timeout,
    Cancelled,
    MalformedResponse,
    EmptyResponse,
    ContextLimit,
    OutputLimit,
    UnexpectedResponse,
};

struct ProviderError final {
    ProviderErrorCode code{ProviderErrorCode::UnexpectedResponse};
    std::string message;

    friend bool operator==(const ProviderError&, const ProviderError&) = default;
};

using LlmResult = std::variant<ProviderResponse, ProviderError>;

class LlmProvider {
public:
    virtual ~LlmProvider() = default;

    // This operation may block for at most request.timeout. Callers that own a UI
    // must invoke it from a worker thread. Cancellation is cooperative and may be
    // requested from any thread.
    [[nodiscard]] virtual LlmResult generate(
        const LlmRequest& request, CancellationToken cancellation = {}) = 0;
};

} // namespace horusrf::ai
