#include "test_support.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

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
        std::cout << "All HorusRF retrieval corpus tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
