#include "test_support.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
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

std::string message_digest(const ai::LlmRequest& request) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto add = [&hash](unsigned char byte) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    };
    for (const auto& message : request.messages) {
        add(static_cast<unsigned char>(message.role));
        std::uint64_t size = message.content.size();
        for (int index = 0; index < 8; ++index) {
            add(static_cast<unsigned char>((size >> (index * 8)) & 0xffU));
        }
        for (const unsigned char byte : message.content) add(byte);
    }
    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}

const ai::RetrievalExample* corpus_example(const ai::RetrievalCorpus& corpus,
                                           std::string_view id) {
    for (const auto& example : corpus.examples) {
        if (example.metadata.id == id) return &example;
    }
    return nullptr;
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
    CHECK(contains(first, "Semicolons are not part of HorusRF syntax"));
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
    CHECK_EQ(ai::prompt_configuration_id(ai::PromptConfiguration::StructuralRag),
             "structural-rag-v1");
    CHECK_EQ(ai::parse_prompt_configuration("structural-rag-v1"),
             std::optional{ai::PromptConfiguration::StructuralRag});
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

void exact_legacy_message_regression_test() {
    const ai::GenerationRequest generation{ai::NaturalLanguageInstruction{
        "Sweep frequency from 900 MHz to 1 GHz and measure power."}};
    const ai::ModificationRequest modification{
        ai::ScriptSource{std::string{valid_source}},
        ai::NaturalLanguageInstruction{"Change only the reference power to -5 dBm."}};
    const ai::ValidationDiagnostic diagnostic{
        ai::DiagnosticCategory::Semantic,
        "semantic.unexpected_quantity_type",
        "expected Power, got PowerDelta",
        {{1, 21, 20}, {1, 25, 24}},
    };
    const ai::RepairPromptRequest repair{
        ai::NaturalLanguageInstruction{"Create a basic power sweep."},
        ai::ScriptSource{"characterize broken { reference power = 1 dB }"},
        ai::ValidationResult::invalid({diagnostic}),
        ai::ScriptSource{std::string{valid_source}},
    };
    ai::RequirementOutcome violation;
    violation.requirement_id = "reference-power-1";
    violation.status = ai::RequirementOutcomeStatus::Violated;
    violation.code = "reference_power_mismatch";
    violation.expected = "-5 dBm";
    violation.actual = "-10 dBm";
    violation.explanation = "reference power differs";
    violation.source_text = "reference power -5 dBm";
    const ai::RequirementCorrectionPromptRequest correction{
        ai::NaturalLanguageInstruction{"Use reference power -5 dBm."},
        ai::ScriptSource{std::string{valid_source}},
        {ai::RequirementSatisfactionStatus::Violated, {violation}},
        std::nullopt,
    };

    const ai::PromptBuilder baseline;
    const ai::PromptBuilder compact{{
        "qwen2.5-coder:7b", 0.0, 2048, std::chrono::minutes{2},
        ai::PromptConfiguration::Compact}};
    const ai::PromptBuilder relevant{{
        "qwen2.5-coder:7b", 0.0, 2048, std::chrono::minutes{2},
        ai::PromptConfiguration::CapabilityRelevant}};

    CHECK_EQ(message_digest(baseline.build_generation(generation)),
             "fnv1a64:7d481aa6de9e7731");
    CHECK_EQ(message_digest(baseline.build_modification(modification)),
             "fnv1a64:21c742a4b3b65e4a");
    CHECK_EQ(message_digest(baseline.build_repair(repair)),
             "fnv1a64:7869130f31870fcf");
    CHECK_EQ(message_digest(baseline.build_requirement_correction(correction)),
             "fnv1a64:12e8c392bfef3e7f");
    CHECK_EQ(message_digest(compact.build_generation(generation)),
             "fnv1a64:8afd45805bb333de");
    CHECK_EQ(message_digest(relevant.build_generation(generation)),
             "fnv1a64:3f0b91e3a25de829");
}

void structural_rag_prompt_test() {
    const auto loaded = ai::load_retrieval_corpus(
        std::filesystem::path{HORUSRF_SOURCE_DIR} / "ai" / "retrieval");
    CHECK(loaded.ok());
    auto retriever = std::make_shared<const ai::ExampleRetriever>(*loaded.corpus);
    const ai::PromptBuilder builder{{
        "qwen2.5-coder:7b", 0.0, 2048, std::chrono::minutes{2},
        ai::PromptConfiguration::StructuralRag, retriever,
        {2, 4096, ai::RetrievalBudgetUnit::Utf8Bytes}}};
    const std::string instruction =
        "Sweep 700 to 760 MHz and require measured power to be strictly greater than 1 dBm.";
    const auto first = builder.build_generation(
        {ai::NaturalLanguageInstruction{instruction}});
    const auto second = builder.build_generation(
        {ai::NaturalLanguageInstruction{instruction}});

    CHECK(first.retrieval != nullptr);
    CHECK(first.retrieval->ok());
    CHECK(!first.retrieval->selected.empty());
    CHECK(first.retrieval->selected.size() <= std::size_t{2});
    CHECK_EQ(first.retrieval->corpus_content_digest, loaded.corpus->content_digest);
    CHECK_EQ(first.retrieval->ranking_id, ai::retrieval_ranking_id());
    CHECK_EQ(first.messages, second.messages);
    CHECK_EQ(first.messages.back().role, ai::LlmRole::User);
    CHECK(first.messages.back().content.find(instruction) != std::string::npos);
    CHECK(first.messages.front().content.find("<retrieved_example") ==
          std::string::npos);

    for (std::size_t selected_index = 0;
         selected_index < first.retrieval->selected.size(); ++selected_index) {
        const auto& selected = first.retrieval->selected[selected_index];
        const auto* example = corpus_example(retriever->corpus(), selected.id);
        CHECK(example != nullptr);
        const auto user_index = 1U + selected_index * 2U;
        const auto assistant_index = user_index + 1U;
        CHECK(first.messages[user_index].content.find(example->request) !=
              std::string::npos);
        CHECK(first.messages[user_index].content.find(selected.id) !=
              std::string::npos);
        CHECK(first.messages[user_index].content.find("untrusted data") !=
              std::string::npos);
        const auto first_line = example->program.substr(0, example->program.find('\n'));
        CHECK(first.messages[assistant_index].content.find(first_line) !=
              std::string::npos);
        CHECK(first.messages[assistant_index].content.find("\"status\":\"proposal\"") !=
              std::string::npos);
    }

    const auto modification = builder.build_modification({
        ai::ScriptSource{std::string{valid_source}},
        ai::NaturalLanguageInstruction{"Change only the reference power."}});
    CHECK(modification.retrieval == nullptr);
    CHECK(!contains(modification, "<retrieved_example"));
}

void structural_rag_fallback_test() {
    const ai::PromptBuilder builder{{
        "qwen2.5-coder:7b", 0.0, 2048, std::chrono::minutes{2},
        ai::PromptConfiguration::StructuralRag}};
    const std::string instruction = "Create a frequency characterization.";
    const auto request = builder.build_generation(
        {ai::NaturalLanguageInstruction{instruction}});
    CHECK(request.retrieval != nullptr);
    CHECK(!request.retrieval->ok());
    CHECK_EQ(request.retrieval->diagnostics.size(), std::size_t{1});
    CHECK_EQ(request.retrieval->diagnostics.front().code,
             ai::RetrievalDiagnosticCode::RetrieverUnavailable);
    CHECK(!contains(request, "<retrieved_example"));
    CHECK(request.messages.back().content.find(instruction) != std::string::npos);
    CHECK(contains(request, "Preserve every explicit operation"));
    CHECK(contains(request, "Authoritative ANTLR grammar:"));

    const auto loaded = ai::load_retrieval_corpus(
        std::filesystem::path{HORUSRF_SOURCE_DIR} / "ai" / "retrieval");
    CHECK(loaded.ok());
    auto retriever = std::make_shared<const ai::ExampleRetriever>(*loaded.corpus);
    const ai::PromptBuilder no_evidence{{
        "qwen2.5-coder:7b", 0.0, 2048, std::chrono::minutes{2},
        ai::PromptConfiguration::StructuralRag, retriever}};
    const auto no_evidence_request = no_evidence.build_generation(
        {ai::NaturalLanguageInstruction{"Please do the thing."}});
    CHECK(no_evidence_request.retrieval != nullptr);
    CHECK(!no_evidence_request.retrieval->ok());
    CHECK_EQ(no_evidence_request.retrieval->diagnostics.front().code,
             ai::RetrievalDiagnosticCode::NoStructuralEvidence);
    CHECK(!contains(no_evidence_request, "<retrieved_example"));

    const ai::PromptBuilder no_budget{{
        "qwen2.5-coder:7b", 0.0, 2048, std::chrono::minutes{2},
        ai::PromptConfiguration::StructuralRag, retriever,
        {1, 0, ai::RetrievalBudgetUnit::Utf8Bytes}}};
    const auto no_budget_request = no_budget.build_generation(
        {ai::NaturalLanguageInstruction{
            "Sweep frequency from 1 GHz to 2 GHz and measure power."}});
    CHECK(no_budget_request.retrieval != nullptr);
    CHECK(!no_budget_request.retrieval->ok());
    CHECK_EQ(no_budget_request.retrieval->diagnostics.front().code,
             ai::RetrievalDiagnosticCode::NoMatchingExample);
    CHECK(!no_budget_request.retrieval->rejected_by_budget.empty());
    CHECK(!contains(no_budget_request, "<retrieved_example"));
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
        {{1, 21, 20}, {1, 25, 24}},
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
    CHECK(contains(request, "semantic semantic.unexpected_quantity_type at 1:21"));
    CHECK(contains(request, diagnostic.message));
    CHECK(contains(request, "1 |"));
    CHECK(contains(request, "Compiler context: Compiler quantity types:"));
    CHECK(contains(request, "The script field is authoritative"));
    CHECK(contains(request, "Merely describing the correction"));
    CHECK(contains(request, "a later diagnostic may be a consequence"));
    CHECK_EQ(request.purpose, ai::LlmCallPurpose::CompilerRepair);
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

void temperature_delta_repair_guidance_test() {
    const std::string source =
        "characterize thermal {\n"
        "  sweep temperature 20 degC .. 40 degC step 10 degC\n"
        "}\n";
    const auto invalid = source.find("10 degC");
    const ai::ValidationDiagnostic diagnostic{
        ai::DiagnosticCategory::Semantic,
        "semantic.unexpected_quantity_type",
        "expected TemperatureDelta, got Temperature",
        {{2, 48, invalid}, {2, 55, invalid + std::string_view{"10 degC"}.size()}},
    };
    const ai::PromptBuilder builder;
    const auto request = builder.build_repair({
        ai::NaturalLanguageInstruction{"Sweep temperature from 20 to 40 degrees."},
        ai::ScriptSource{source},
        ai::ValidationResult::invalid({diagnostic}),
        std::nullopt,
    });

    CHECK(contains(request, "Valid units for TemperatureDelta: deltaK, deltaC"));
    CHECK(contains(request, "The invalid source span is: 10 degC"));
    CHECK(contains(request, "temperature steps use deltaC or deltaK"));
    CHECK(contains(request, "step 10 deltaC"));
    CHECK(contains(request, "Edit the script field itself"));
}

void missing_characterization_identifier_repair_guidance_test() {
    const ai::PromptBuilder builder;
    const std::string candidate =
        "characterize { reference power = 0 dBm; sweep frequency 3 GHz .. 5 GHz "
        "step 1 GHz; sweep temperature 40 degC .. 80 degC step 10 degC; }";
    const ai::ValidationDiagnostic diagnostic{
        ai::DiagnosticCategory::Syntax,
        "parse.expected_identifier",
        "expected identifier",
        {{1, 14, 13}, {1, 15, 14}},
    };

    const auto request = builder.build_repair({
        ai::NaturalLanguageInstruction{
            "characterize from 3 to 5 Ghz and from 40 to 80 degress"},
        ai::ScriptSource{candidate},
        ai::ValidationResult::invalid({diagnostic}),
        std::nullopt,
    });

    CHECK(contains(request, "the required characterization name is missing"));
    CHECK(contains(request,
                   "characterize frequency_temperature_characterization {"));
    CHECK(contains(request, "Do not add an identifier to `sweep temperature`"));
    CHECK(contains(request, "remove every `;`"));
    CHECK(contains(request, "its step must use deltaC or deltaK"));
    CHECK(contains(request, "the `..` range token is mandatory"));
    CHECK(contains(request, "Edit the script field itself"));
}

void requirement_correction_context_test() {
    ai::RequirementOutcome violation;
    violation.requirement_id = "reference-power-1";
    violation.status = ai::RequirementOutcomeStatus::Violated;
    violation.code = "reference_power_mismatch";
    violation.expected = "-5 dBm";
    violation.actual = "-10 dBm";
    violation.explanation = "reference power differs";
    violation.source_text = "reference power -5 dBm";
    const ai::RequirementCorrectionPromptRequest input{
        ai::NaturalLanguageInstruction{"Use reference power -5 dBm."},
        ai::ScriptSource{std::string{valid_source}},
        {ai::RequirementSatisfactionStatus::Violated, {violation}},
        std::nullopt,
    };
    const ai::PromptBuilder builder;
    const auto request = builder.build_requirement_correction(input);
    CHECK_EQ(request.purpose, ai::LlmCallPurpose::RequirementCorrection);
    CHECK(contains(request, "<requirement_violations>"));
    CHECK(contains(request, violation.code));
    CHECK(contains(request, violation.expected));
    CHECK(contains(request, "Preserve every unaffected statement"));

    violation.confidence = ai::RequirementConfidence::Ambiguous;
    bool threw = false;
    try {
        static_cast<void>(builder.build_requirement_correction(
            {input.original_instruction, input.previous_candidate,
             {ai::RequirementSatisfactionStatus::Unknown, {violation}},
             std::nullopt}));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

void parser_repair_context_test() {
    const ai::ValidationDiagnostic diagnostic{
        ai::DiagnosticCategory::Syntax,
        "parse.unexpected_token",
        "unexpected token",
        {{1, 23, 22}, {1, 24, 23}},
    };
    const ai::PromptBuilder builder;
    const auto request = builder.build_repair(
        {ai::NaturalLanguageInstruction{"Create a basic characterization."},
         ai::ScriptSource{"characterize broken { @ }"},
         ai::ValidationResult::invalid({diagnostic}), std::nullopt});
    CHECK(contains(request, "parse.unexpected_token at 1:23-1:24"));
    CHECK(contains(request, "Compiler context: Compiler syntax tokens:"));
    CHECK(contains(request, "Nearby candidate lines:"));
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
        exact_legacy_message_regression_test();
        capability_relevant_selection_test();
        structural_rag_prompt_test();
        structural_rag_fallback_test();
        modification_context_test();
        repair_context_test();
        temperature_delta_repair_guidance_test();
        missing_characterization_identifier_repair_guidance_test();
        requirement_correction_context_test();
        parser_repair_context_test();
        structured_response_test();
        structured_response_rejection_test();
        std::cout << "All HorusRF AI prompt tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
