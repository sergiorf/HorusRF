#include "test_support.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "horusrf/ai/retrieval.hpp"

namespace {

namespace fs = std::filesystem;
using namespace horusrf;

const fs::path fixture_root = fs::path{HORUSRF_BINARY_DIR} / "retrieval-corpus-fixtures";

struct Cleanup final { ~Cleanup() { fs::remove_all(fixture_root); } };

void write(const fs::path& path, std::string_view content) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << content;
    if (!output) throw std::runtime_error("failed to write fixture " + path.string());
}

std::string manifest(std::string_view examples, std::string_view digest) {
    return std::string{R"({"schema":"horusrf.retrieval-corpus","version":1,"corpus_id":"fixture","corpus_version":"1.0.0","content_digest":")"}
        + std::string{digest} + R"(","examples":[)" + std::string{examples} + "]}\n";
}

std::string metadata(std::string_view id) {
    return std::string{R"({"schema":"horusrf.retrieval-example","version":1,"id":")"}
        + std::string{id} +
        R"(","category":"fixture","operations":["measure_power"],"structural_steps":["measure_named_power"],"sweep_dimensions":[],"dependencies":[],"comparison_strictness":"none","complexity":"basic","provenance":{"origin":"test fixture","authoring_date":"2026-10-24","influenced_by":[]}})" "\n";
}

constexpr std::string_view valid_program = R"(characterize fixture {
  reference power = -10 dBm
  sweep frequency 1 GHz .. 1 GHz step 1 MHz
  measure observed = power
  derive calibration fixture_table {
    correction = reference.power - observed
    over frequency
  }
}
)";

void add_example(const fs::path& root, std::string_view directory,
                 std::string_view id, std::string_view program = valid_program) {
    const auto path = root / "examples" / directory;
    write(path / "request.txt", "Record one named output-power observation.\n");
    write(path / "program.hrf", program);
    write(path / "metadata.json", metadata(id));
}

void seal(const fs::path& root, std::string_view examples) {
    write(root / "manifest.json", manifest(examples, "fnv1a64:0000000000000000"));
    const auto digest = ai::retrieval_corpus_content_digest(root);
    write(root / "manifest.json", manifest(examples, digest));
}

bool has_message(const ai::RetrievalCorpusResult& result, std::string_view part) {
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.message.find(part) != std::string::npos) return true;
    }
    return false;
}

bool has_diagnostic(const ai::RetrievalResult& result,
                    ai::RetrievalDiagnosticCode code) {
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == code) return true;
    }
    return false;
}

ai::RetrievalCorpus checked_in_corpus() {
    const auto root = fs::path{HORUSRF_SOURCE_DIR} / "ai" / "retrieval";
    const auto result = ai::load_retrieval_corpus(root);
    if (!result.ok()) {
        throw std::runtime_error(result.diagnostics.front().path.string() + ": " +
                                 result.diagnostics.front().message);
    }
    return *result.corpus;
}

ai::RetrievalExample example(std::string id, std::string request,
                             std::string program = std::string{valid_program}) {
    return {{std::move(id), "fixture", {"measure_power"},
             {"measure_named_power"}, {}, {}, "none", "basic",
             {"test fixture", "2026-10-25", {}}},
            std::move(request), std::move(program), {}};
}

void checked_in_corpus_test() {
    const auto root = fs::path{HORUSRF_SOURCE_DIR} / "ai" / "retrieval";
    const auto comparison = fs::path{HORUSRF_SOURCE_DIR} / "tests" / "ai" / "golden";
    const auto result = ai::load_retrieval_corpus(root, comparison);
    if (!result.ok()) {
        throw std::runtime_error(result.diagnostics.front().path.string() + ": " +
                                 result.diagnostics.front().message);
    }
    CHECK_EQ(result.corpus->corpus_id, "horusrf-structural-examples");
    CHECK_EQ(result.corpus->corpus_version, "1.0.0");
    CHECK_EQ(result.corpus->examples.size(), std::size_t{20});
    CHECK_EQ(result.corpus->examples.front().metadata.id, "fixed_power_observation");
    CHECK_EQ(result.corpus->examples.back().metadata.id,
             "mixed_comparison_qualification");
    CHECK_EQ(result.corpus->leakage_report.comparison_dataset_id, "2.0.0");
    CHECK(result.corpus->leakage_report.findings.empty());
    for (const auto& example : result.corpus->examples) {
        CHECK(!example.request.empty());
        CHECK(!example.program.empty());
        CHECK(!example.metadata.operations.empty());
        CHECK(!example.metadata.structural_steps.empty());
        CHECK_EQ(example.metadata.provenance.authoring_date, "2026-10-24");
    }
    const auto report = fixture_root / "leakage-report.json";
    fs::create_directories(report.parent_path());
    ai::write_leakage_report(report, *result.corpus);
    CHECK(fs::file_size(report) > 100U);
}

void exact_schema_and_digest_test() {
    const auto root = fixture_root / "schema";
    add_example(root, "one", "one");
    seal(root, "\"one\"");
    CHECK(ai::load_retrieval_corpus(root).ok());

    auto bad_metadata = metadata("one");
    bad_metadata.insert(bad_metadata.find_last_of('}'), ",\"extra\":true");
    write(root / "examples" / "one" / "metadata.json", bad_metadata);
    auto result = ai::load_retrieval_corpus(root);
    CHECK(!result.ok());
    CHECK(has_message(result, "unknown member 'extra'"));

    write(root / "examples" / "one" / "metadata.json", metadata("one"));
    write(root / "examples" / "one" / "request.txt", "Changed after sealing.\n");
    result = ai::load_retrieval_corpus(root);
    CHECK(!result.ok());
    CHECK(has_message(result, "content_digest mismatch"));
}

void identity_order_and_completeness_test() {
    const auto root = fixture_root / "identity";
    add_example(root, "first", "duplicate");
    add_example(root, "second", "duplicate");
    add_example(root, "unlisted", "unlisted");
    seal(root, "\"first\",\"second\"");
    const auto result = ai::load_retrieval_corpus(root);
    CHECK(!result.ok());
    CHECK(has_message(result, "does not match directory name"));
    CHECK(has_message(result, "duplicate example id 'duplicate'"));
    CHECK(has_message(result, "example directory is not listed in manifest"));
}

void compiler_and_utf8_test() {
    const auto invalid = fixture_root / "compiler";
    add_example(invalid, "one", "one", "characterize broken { measure @ }\n");
    seal(invalid, "\"one\"");
    auto result = ai::load_retrieval_corpus(invalid);
    CHECK(!result.ok());
    CHECK(has_message(result, "failed compiler validation"));

    const auto utf8 = fixture_root / "utf8";
    add_example(utf8, "one", "one");
    write(utf8 / "examples" / "one" / "request.txt", std::string{"bad\xc3\x28", 5});
    seal(utf8, "\"one\"");
    result = ai::load_retrieval_corpus(utf8);
    CHECK(!result.ok());
    CHECK(has_message(result, "not valid UTF-8"));
}

void internal_duplication_report_test() {
    const auto root = fixture_root / "duplicates";
    add_example(root, "one", "one");
    add_example(root, "two", "two");
    seal(root, "\"one\",\"two\"");
    const auto result = ai::load_retrieval_corpus(root);
    CHECK(result.ok());
    CHECK_EQ(result.corpus->leakage_report.findings.size(), std::size_t{2});
    CHECK_EQ(result.corpus->leakage_report.findings.front().comparison_id,
             "corpus:two");
}

void query_analysis_test() {
    const auto query = ai::analyze_retrieval_query(
        "First sweep temperature from 20 degC to 40 degC in 10 deltaC steps, "
        "then sweep frequency from 1 GHz to 2 GHz in 100 MHz steps. Record EVM "
        "named quality, require EVM below 2%, and derive a calibration over "
        "temperature and frequency.");
    CHECK_EQ(query.sweep_dimensions.size(), std::size_t{2});
    CHECK_EQ(query.sweep_dimensions[0], "temperature");
    CHECK_EQ(query.sweep_dimensions[1], "frequency");
    CHECK(std::ranges::find(query.operations, "measure_evm") !=
          query.operations.end());
    CHECK(std::ranges::find(query.operations, "assert_evm") !=
          query.operations.end());
    CHECK(std::ranges::find(query.operations, "derive_calibration") !=
          query.operations.end());
    CHECK_EQ(query.comparison_strictness, "strict");
    CHECK(query.calibration);

    const auto partial = ai::analyze_retrieval_query(
        "Sweep output power, then sweep frequency from 1 GHz to 2 GHz in 1 MHz steps.");
    CHECK(std::ranges::find(partial.unsupported_operations, "sweep.power") !=
          partial.unsupported_operations.end());
    CHECK(std::ranges::find(partial.operations, "sweep_frequency") !=
          partial.operations.end());
    CHECK(std::ranges::find(partial.operations, "sweep_power") ==
          partial.operations.end());

    const auto ambiguous = ai::analyze_retrieval_query("Use a power range.");
    CHECK(ambiguous.unknown_evidence_count > 0U);
    CHECK(ambiguous.operations.empty());
}

void structural_ranking_and_paraphrase_test() {
    const ai::ExampleRetriever retriever{checked_in_corpus()};
    const auto exact = retriever.retrieve(
        "Sweep 700 to 760 MHz and require measured power to be strictly greater "
        "than 1 dBm.",
        {1, 4096, ai::RetrievalBudgetUnit::Utf8Bytes});
    CHECK(exact.ok());
    CHECK_EQ(exact.selected.front().id, "strict_power_guard");

    const std::string request =
        "Make temperature the first sweep from -30 to 30 degC in 15 deltaC "
        "increments, then sweep 1.1 to 1.3 GHz by 50 MHz and record named power.";
    const auto first = retriever.retrieve(request, {1, 4096,
                                                    ai::RetrievalBudgetUnit::Utf8Bytes});
    CHECK(first.ok());
    CHECK_EQ(first.ranking_id, "structural-rag-ranking-v1");
    CHECK_EQ(first.selected.size(), std::size_t{1});
    CHECK_EQ(first.selected.front().id, "temperature_then_frequency");
    CHECK(first.ranked_candidates.size() > first.selected.size());
    CHECK_EQ(first.ranked_candidates.front().id, first.selected.front().id);
    CHECK(std::ranges::is_sorted(first.ranked_candidates,
        [](const auto& left, const auto& right) {
            return left.scores.total > right.scores.total;
        }));
    CHECK(first.selected.front().scores.structural >
          first.selected.front().scores.lexical_similarity);
    CHECK(!first.selected.front().rationale.empty());
    CHECK(first.inserted_context_bytes > 0U);
    CHECK(first.duration >= std::chrono::nanoseconds::zero());

    const auto second = retriever.retrieve(request, {1, 4096,
                                                     ai::RetrievalBudgetUnit::Utf8Bytes});
    CHECK_EQ(second.selected.front().id, first.selected.front().id);
    CHECK_EQ(second.selected.front().scores.total,
             first.selected.front().scores.total);
    CHECK_EQ(second.ranked_candidates.size(), first.ranked_candidates.size());

    const auto dependency = retriever.retrieve(
        "Measure output relative to -13 dBm, derive power error, then derive an "
        "inverted delta from that first result.",
        {1, 4096, ai::RetrievalBudgetUnit::Utf8Bytes});
    CHECK(dependency.ok());
    CHECK_EQ(dependency.selected.front().id, "two_stage_derivation");
}

void ties_duplicates_and_budgets_test() {
    ai::RetrievalCorpus tied{"fixture", "1.0.0", "digest",
        {example("manifest_first", "alpha observation"),
         example("manifest_second", "beta observation")}, {}};
    const ai::ExampleRetriever tie_retriever{std::move(tied)};
    const auto tie = tie_retriever.retrieve(
        "Record power.", {1, 4096, ai::RetrievalBudgetUnit::Utf8Bytes});
    CHECK(tie.ok());
    CHECK_EQ(tie.selected.front().id, "manifest_first");

    auto first = example("first", "Record one power value.");
    auto duplicate = example("duplicate", "Record one power value.");
    auto distinct = example("distinct", "Observe transmitter power separately.",
                            std::string{valid_program} + "\n// distinct\n");
    distinct.metadata.structural_steps.push_back("distinct_shape");
    ai::RetrievalCorpus duplicates{"fixture", "1.0.0", "digest",
        {std::move(first), std::move(duplicate), std::move(distinct)}, {}};
    const ai::ExampleRetriever duplicate_retriever{std::move(duplicates)};
    const auto selected = duplicate_retriever.retrieve(
        "Record power.", {3, 4096, ai::RetrievalBudgetUnit::Utf8Bytes});
    CHECK_EQ(selected.selected.size(), std::size_t{1});
    CHECK_EQ(selected.suppressed_near_duplicates.size(), std::size_t{2});

    const auto rejected = duplicate_retriever.retrieve(
        "Record power.", {2, 0, ai::RetrievalBudgetUnit::Utf8Bytes});
    CHECK(!rejected.ok());
    CHECK(has_diagnostic(rejected, ai::RetrievalDiagnosticCode::NoMatchingExample));
    CHECK(!rejected.rejected_by_budget.empty());

    const auto token_budget = tie_retriever.retrieve(
        "Record power.", {1, 1000, ai::RetrievalBudgetUnit::EstimatedTokens});
    CHECK(token_budget.ok());
    CHECK(token_budget.inserted_context_estimated_tokens <= 1000U);
}

void safe_fallback_test() {
    const ai::ExampleRetriever empty{ai::RetrievalCorpus{
        "empty", "1.0.0", "digest", {}, {}}};
    auto result = empty.retrieve("Record power.");
    CHECK(!result.ok());
    CHECK(has_diagnostic(result, ai::RetrievalDiagnosticCode::EmptyCorpus));

    const ai::ExampleRetriever retriever{checked_in_corpus()};
    result = retriever.retrieve(" ");
    CHECK(has_diagnostic(result, ai::RetrievalDiagnosticCode::EmptyInstruction));
    result = retriever.retrieve("Record power.", {4, 4096,
                                                  ai::RetrievalBudgetUnit::Utf8Bytes});
    CHECK(has_diagnostic(result, ai::RetrievalDiagnosticCode::InvalidTopK));
    result = retriever.retrieve("Please do the thing.");
    CHECK(has_diagnostic(result,
                         ai::RetrievalDiagnosticCode::NoStructuralEvidence));
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view{argv[1]} == "--print-digest") {
            std::cout << ai::retrieval_corpus_content_digest(
                fs::path{HORUSRF_SOURCE_DIR} / "ai" / "retrieval") << '\n';
            return 0;
        }
        if (argc == 2 && std::string_view{argv[1]} == "--write-leakage-report") {
            const auto source = fs::path{HORUSRF_SOURCE_DIR};
            const auto result = ai::load_retrieval_corpus(
                source / "ai" / "retrieval", source / "tests" / "ai" / "golden");
            if (!result.ok()) throw std::runtime_error("cannot validate retrieval corpus");
            ai::write_leakage_report(source / "ai" / "retrieval" /
                                         "leakage-report.json",
                                     *result.corpus);
            return 0;
        }
        Cleanup cleanup;
        fs::remove_all(fixture_root);
        checked_in_corpus_test();
        exact_schema_and_digest_test();
        identity_order_and_completeness_test();
        compiler_and_utf8_test();
        internal_duplication_report_test();
        query_analysis_test();
        structural_ranking_and_paraphrase_test();
        ties_duplicates_and_budgets_test();
        safe_fallback_test();
        std::cout << "All HorusRF retrieval corpus tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
