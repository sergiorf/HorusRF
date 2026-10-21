#include "test_support.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "horusrf/ai/prompt.hpp"
#include "horusrf/ai/validation.hpp"
#include "horusrf/frontend/language_services.hpp"

namespace {

using namespace horusrf;

constexpr std::string_view valid_source = R"(characterize source {
  reference power = -10 dBm
  sweep frequency 1 GHz .. 2 GHz step 1 GHz
  measure power
  derive calibration cal {
    correction = reference.power - power
    over frequency
  }
}
)";

bool contains(const ai::LlmRequest& request, std::string_view text) {
    for (const auto& message : request.messages) {
        if (message.content.find(text) != std::string::npos) return true;
    }
    return false;
}

std::size_t prompt_bytes(const ai::LlmRequest& request) {
    std::size_t result{};
    for (const auto& message : request.messages) result += message.content.size();
    if (request.json_schema) result += request.json_schema->size();
    return result;
}

const ai::ProposalResponseError& response_error(const ai::ProposalResponseResult& result) {
    CHECK(std::holds_alternative<ai::ProposalResponseError>(result));
    return std::get<ai::ProposalResponseError>(result);
}

void compiler_derived_generation_prompt_test() {
    const ai::PromptBuilder builder;
    const ai::GenerationRequest input{
        ai::NaturalLanguageInstruction{"Sweep 900 MHz to 1 GHz and measure power."}};
    const auto first = builder.build_generation(input);
    const auto second = builder.build_generation(input);

    CHECK_EQ(first.messages, second.messages);
    CHECK_EQ(first.model, "qwen2.5-coder:7b");
    CHECK_EQ(first.temperature, 0.0);
    CHECK_EQ(first.max_output_tokens, std::size_t{2048});
    CHECK(first.json_schema.has_value());
    CHECK_EQ(*first.json_schema, ai::proposal_response_schema());
    CHECK_EQ(first.messages.front().role, ai::LlmRole::System);
    CHECK(contains(first, "Measurements: power -> Power, evm -> Ratio, frequency_error -> FrequencyDelta"));
    CHECK(contains(first, "Authoritative ANTLR grammar:"));
    CHECK(contains(first, "program: characterization EOF;"));
    CHECK(contains(first, "measurementStatement"));
    CHECK(contains(first, "Fixed syntax tokens: characterize, reference, power"));
    CHECK(contains(first, "temperature (endpoints Temperature, step TemperatureDelta, optional)"));
    CHECK(contains(first, "calibration.all_sweeps_in_order"));
    CHECK(contains(first, "Power - Power -> PowerDelta"));
    CHECK(contains(first, "Unsupported features include conditionals"));
    CHECK(contains(first, "Never invent language constructs"));
    CHECK(contains(first, "Do not claim compiler validation"));
    CHECK(contains(first, input.instruction.text()));

    const auto& capabilities = frontend::get_language_capabilities();
    for (const auto& measurement : capabilities.measurements) {
        CHECK(contains(first, measurement.operation));
        CHECK(contains(first, measurement.result_type));
    }
    for (const auto& unit : capabilities.units) CHECK(contains(first, unit.spelling));
}

void custom_settings_and_input_validation_test() {
    const ai::PromptBuilder builder{{"selected-model", 0.2, 512,
                                     std::chrono::milliseconds{3210}}};
    const auto request = builder.build_generation(
        {ai::NaturalLanguageInstruction{"Create a frequency characterization."}});
    CHECK_EQ(request.model, "selected-model");
    CHECK_EQ(request.temperature, 0.2);
    CHECK_EQ(request.max_output_tokens, std::size_t{512});
    CHECK_EQ(request.timeout, std::chrono::milliseconds{3210});

    bool threw = false;
    try {
        static_cast<void>(ai::PromptBuilder{{"", 0.0, 1, std::chrono::seconds{1}}});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        static_cast<void>(builder.build_generation(
            {ai::NaturalLanguageInstruction{" \n"}}));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

void curated_examples_compile_test() {
    const auto& examples = ai::curated_prompt_examples();
    CHECK_EQ(examples.size(), std::size_t{4});
    bool saw_temperature = false;
    bool saw_evm = false;
    bool saw_frequency_error = false;
    bool saw_assertion = false;
    bool saw_modification = false;
    bool saw_repository_comment = false;
    for (const auto& example : examples) {
        CHECK(ai::validate_script(example.proposal.text()).ok());
        if (example.current_source) {
            CHECK(ai::validate_script(example.current_source->text()).ok());
        }
        saw_temperature = saw_temperature ||
                          example.proposal.text().find("sweep temperature") != std::string::npos;
        saw_evm = saw_evm || example.proposal.text().find("= evm") != std::string::npos;
        saw_frequency_error = saw_frequency_error ||
                              example.proposal.text().find("= frequency_error") != std::string::npos;
        saw_assertion = saw_assertion ||
                        example.proposal.text().find("assert ") != std::string::npos;
        saw_modification = saw_modification || example.current_source.has_value();
        saw_repository_comment = saw_repository_comment ||
                                 example.proposal.text().starts_with("// Characterizes");
    }
    CHECK(saw_temperature);
    CHECK(saw_evm);
    CHECK(saw_frequency_error);
    CHECK(saw_assertion);
    CHECK(saw_modification);
    CHECK(saw_repository_comment);
}

void named_prompt_configuration_test() {
    CHECK_EQ(ai::prompt_configuration_id(ai::PromptConfiguration::Baseline),
             "direct-dsl-v1");
    CHECK_EQ(ai::parse_prompt_configuration("direct-dsl-compact-v1"),
             std::optional{ai::PromptConfiguration::Compact});
    CHECK_EQ(ai::parse_prompt_configuration("direct-dsl-relevant-v1"),
             std::optional{ai::PromptConfiguration::CapabilityRelevant});
    CHECK(!ai::parse_prompt_configuration("unknown"));

    const ai::GenerationRequest input{ai::NaturalLanguageInstruction{
        "Sweep frequency from 900 MHz to 1 GHz and measure power."}};
    const auto baseline = ai::PromptBuilder{}.build_generation(input);
    const auto compact = ai::PromptBuilder{{
        "qwen2.5-coder:7b", 0.0, 2048, std::chrono::minutes{2},
        ai::PromptConfiguration::Compact}}.build_generation(input);
    CHECK(prompt_bytes(compact) < prompt_bytes(baseline));
    CHECK(contains(compact, "Preserve every explicit operation"));
    CHECK(!contains(compact, "// Characterizes"));
}

void capability_relevant_selection_test() {
    const ai::PromptBuilder builder{{
        "qwen2.5-coder:7b", 0.0, 2048, std::chrono::minutes{2},
        ai::PromptConfiguration::CapabilityRelevant}};

    const auto frequency = builder.build_generation({ai::NaturalLanguageInstruction{
        "Sweep frequency from 900 MHz to 1 GHz and measure power."}});
    CHECK(contains(frequency, "Characterize output power from 2.40 GHz"));
    CHECK(!contains(frequency, "Sweep frequency and temperature"));
    CHECK(!contains(frequency, "Check power error within 1.5 dB"));

    const auto temperature = builder.build_generation({ai::NaturalLanguageInstruction{
        "Sweep temperature and record EVM."}});
    CHECK(contains(temperature, "Sweep frequency and temperature"));
    CHECK(!contains(temperature, "Characterize output power from 2.40 GHz"));
    CHECK(!contains(temperature, "Check power error within 1.5 dB"));

    const auto assertion = builder.build_generation({ai::NaturalLanguageInstruction{
        "Keep the measured power error within 1 dB."}});
    CHECK(contains(assertion, "Check power error within 1.5 dB"));
    CHECK(!contains(assertion, "Sweep frequency and temperature"));

    const auto calibration = builder.build_generation({ai::NaturalLanguageInstruction{
        "Create a calibration correction for measured power."}});
    CHECK(contains(calibration, "Characterize output power from 2.40 GHz"));

    const ai::ModificationRequest modification{
        ai::ScriptSource{std::string{valid_source}},
        ai::NaturalLanguageInstruction{"Change only the frequency step."}};
    const auto modified = builder.build_modification(modification);
    CHECK(contains(modified, "Change only the frequency step to 10 MHz"));
    CHECK(!contains(modified, "Characterize output power from 2.40 GHz"));

    CHECK_EQ(frequency.messages,
             builder.build_generation({ai::NaturalLanguageInstruction{
                 "Sweep frequency from 900 MHz to 1 GHz and measure power."}}).messages);
}

void modification_context_test() {
    const ai::PromptBuilder builder;
    const ai::ModificationRequest input{
        ai::ScriptSource{std::string{valid_source}},
        ai::NaturalLanguageInstruction{"Change only the reference power to -5 dBm."}};
    const auto request = builder.build_modification(input);
    CHECK(contains(request, "<current_horusrf>"));
    CHECK(contains(request, input.current_source.text()));
    CHECK(contains(request, input.instruction.text()));
    CHECK(contains(request, "preserves the remaining experiment"));
    CHECK(contains(request, "Preserve all unrelated source behavior"));
}

void repair_context_test() {
    const ai::ValidationDiagnostic diagnostic{
        ai::DiagnosticCategory::Semantic,
        "semantic.unexpected_quantity_type",
        "expected Power, got PowerDelta",
        {{3, 21, 42}, {3, 25, 46}},
    };
    const ai::RepairPromptRequest input{
        ai::NaturalLanguageInstruction{"Create a basic power sweep."},
        ai::ScriptSource{"characterize broken { reference power = 1 dB }"},
        ai::ValidationResult::invalid({diagnostic}),
        ai::ScriptSource{std::string{valid_source}},
    };
    const ai::PromptBuilder builder;
    const auto request = builder.build_repair(input);
    CHECK(contains(request, input.original_instruction.text()));
    CHECK(contains(request, input.previous_candidate.text()));
    CHECK(contains(request, input.original_source->text()));
    CHECK(contains(request, "semantic semantic.unexpected_quantity_type at 3:21"));
    CHECK(contains(request, diagnostic.message));
    CHECK(!contains(request, "Example request:"));

    bool threw = false;
    try {
        static_cast<void>(builder.build_repair(
            {input.original_instruction, input.previous_candidate,
             ai::ValidationResult::valid(), std::nullopt}));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

void structured_response_test() {
    const auto result = ai::parse_proposal_response(
        R"({"status":"proposal","script":"characterize caf\u00e9 {}\n","explanation":"generated","warnings":["unsupported phase sweep"]})");
    CHECK(std::holds_alternative<ai::ProposalPayload>(result));
    const auto& payload = std::get<ai::ProposalPayload>(result);
    CHECK_EQ(payload.script.text(), "characterize caf\xc3\xa9 {}\n");
    CHECK_EQ(payload.explanation, "generated");
    CHECK_EQ(payload.warnings.size(), std::size_t{1});
    CHECK_EQ(payload.warnings.front(), "unsupported phase sweep");
}

void structured_response_rejection_test() {
    const std::vector<std::string> malformed{
        "not json",
        R"({"status":"proposal",})",
        R"({"status":"proposal","status":"proposal","script":"x","explanation":"x","warnings":[]})",
    };
    for (const auto& input : malformed) {
        CHECK_EQ(response_error(ai::parse_proposal_response(input)).code,
                 ai::ProposalResponseErrorCode::MalformedJson);
    }

    const std::vector<std::string> wrong_shape{
        R"([])",
        R"({"status":"proposal","script":"x","explanation":"x"})",
        R"({"status":"done","script":"x","explanation":"x","warnings":[]})",
        R"({"status":"proposal","script":1,"explanation":"x","warnings":[]})",
        R"({"status":"proposal","script":"x","explanation":"x","warnings":[1]})",
        R"({"status":"proposal","script":"x","explanation":"x","warnings":[],"extra":true})",
    };
    for (const auto& input : wrong_shape) {
        CHECK_EQ(response_error(ai::parse_proposal_response(input)).code,
                 ai::ProposalResponseErrorCode::SchemaViolation);
    }
}

} // namespace

int main() {
    try {
        compiler_derived_generation_prompt_test();
        custom_settings_and_input_validation_test();
        curated_examples_compile_test();
        named_prompt_configuration_test();
        capability_relevant_selection_test();
        modification_context_test();
        repair_context_test();
        structured_response_test();
        structured_response_rejection_test();
        std::cout << "All HorusRF AI prompt tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
