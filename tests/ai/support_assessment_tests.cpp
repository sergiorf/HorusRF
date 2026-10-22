#include "test_support.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

#include "horusrf/ai/support_assessment.hpp"

namespace {

using namespace horusrf;

const ai::SupportClause* find(const ai::SupportAssessment& assessment,
                              const std::string& operation) {
    for (const auto& clause : assessment.clauses) {
        if (clause.operation == operation) return &clause;
    }
    return nullptr;
}

void compiler_capability_assessment_test() {
    const auto frequency = ai::assess_instruction_support(
        "Sweep frequency from 1 GHz to 2 GHz.");
    CHECK_EQ(frequency.status, ai::SupportAssessmentStatus::Supported);
    CHECK(find(frequency, "sweep.frequency") != nullptr);
    CHECK_EQ(find(frequency, "sweep.frequency")->status,
             ai::SupportClauseStatus::Supported);

    const auto temperature = ai::assess_instruction_support(
        "Create a temperature sweep from 20 C to 30 C.");
    CHECK_EQ(temperature.status, ai::SupportAssessmentStatus::Supported);
    CHECK_EQ(find(temperature, "sweep.temperature")->status,
             ai::SupportClauseStatus::Supported);

    const auto fixed = ai::assess_instruction_support(
        "At a fixed 2.45 GHz, measure output power.");
    CHECK_EQ(fixed.status, ai::SupportAssessmentStatus::Supported);
    CHECK_EQ(find(fixed, "fixed.frequency")->code,
             std::string{"supported_fixed_frequency"});
}

void unsupported_partial_and_uncertain_test() {
    const auto unsupported = ai::assess_instruction_support(
        "At a fixed 2.45 GHz, sweep the requested transmit power from -20 dBm to 0 dBm.");
    CHECK_EQ(unsupported.status, ai::SupportAssessmentStatus::Unsupported);
    CHECK_EQ(find(unsupported, "sweep.power")->status,
             ai::SupportClauseStatus::Unsupported);
    CHECK(!find(unsupported, "sweep.power")->source_text.empty());

    const auto partial = ai::assess_instruction_support(
        "Sweep frequency from 1 GHz to 2 GHz and sweep power from -20 dBm to 0 dBm.");
    CHECK_EQ(partial.status, ai::SupportAssessmentStatus::PartiallySupported);
    CHECK(find(partial, "sweep.frequency") != nullptr);
    CHECK(find(partial, "sweep.power") != nullptr);

    const auto ambiguous = ai::assess_instruction_support(
        "Characterize the requested power range.");
    CHECK_EQ(ambiguous.status, ai::SupportAssessmentStatus::Uncertain);
    CHECK_EQ(find(ambiguous, "range.power")->status,
             ai::SupportClauseStatus::Uncertain);
}

void metadata_remains_authoritative_test() {
    auto capabilities = frontend::get_language_capabilities();
    capabilities.sweep_dimensions.push_back({"power", "Power", "PowerDelta", false});
    const auto supported = ai::assess_instruction_support(
        "Sweep transmit power from -20 dBm to 0 dBm.", capabilities);
    CHECK_EQ(supported.status, ai::SupportAssessmentStatus::Supported);
    CHECK_EQ(find(supported, "sweep.power")->status,
             ai::SupportClauseStatus::Supported);

    capabilities.sweep_dimensions.clear();
    const auto frequency = ai::assess_instruction_support(
        "Use a frequency sweep from 1 GHz to 2 GHz.", capabilities);
    CHECK_EQ(frequency.status, ai::SupportAssessmentStatus::Unsupported);
}

} // namespace

int main() {
    try {
        compiler_capability_assessment_test();
        unsupported_partial_and_uncertain_test();
        metadata_remains_authoritative_test();
        std::cout << "All HorusRF AI support assessment tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
