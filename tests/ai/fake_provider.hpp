#pragma once

#include <cstddef>
#include <deque>
#include <utility>
#include <vector>

#include "horusrf/ai/provider.hpp"

namespace horusrf::ai::testing {

class FakeLlmProvider final : public LlmProvider {
public:
    explicit FakeLlmProvider(std::vector<LlmResult> scripted) {
        for (auto& result : scripted) scripted_.push_back(std::move(result));
    }

    [[nodiscard]] LlmResult generate(
        const LlmRequest& request, CancellationToken cancellation = {}) override {
        requests_.push_back(request);
        if (cancellation.stop_requested()) {
            return ProviderError{ProviderErrorCode::Cancelled,
                                 "the fake provider request was cancelled"};
        }
        if (scripted_.empty()) {
            return ProviderError{ProviderErrorCode::UnexpectedResponse,
                                 "the fake provider has no scripted response"};
        }
        LlmResult result = std::move(scripted_.front());
        scripted_.pop_front();
        return result;
    }

    [[nodiscard]] const std::vector<LlmRequest>& requests() const noexcept {
        return requests_;
    }
    [[nodiscard]] std::size_t remaining() const noexcept { return scripted_.size(); }

private:
    std::deque<LlmResult> scripted_;
    std::vector<LlmRequest> requests_;
};

} // namespace horusrf::ai::testing
