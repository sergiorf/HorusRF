#include "antlr_parser.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "horusrf/parser/lexer.hpp"
#include "horusrf/parser/parser.hpp"
#include "horusrf/semantic/analyzer.hpp"
#include "parser/regression_corpus.hpp"
#include "parser/regression_support.hpp"

namespace {

using namespace horusrf;
using namespace horusrf::test::parser_regression;

std::string read_source(std::string_view relative_path) {
    const auto path = std::filesystem::path{HORUSRF_SOURCE_DIR} / relative_path;
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read parity fixture: " + path.string());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

std::string span_snapshot(const parser::SourceSpan& span) {
    const auto position = [](const parser::SourcePosition& value) {
        return std::to_string(value.line) + ':' + std::to_string(value.column) + '@' +
               std::to_string(value.offset);
    };
    return '[' + position(span.begin) + '-' + position(span.end) + ']';
}

std::string semantic_snapshot(const semantic::AnalysisResult& result) {
    std::ostringstream output;
    output << std::setprecision(17) << (result.ok() ? "accepted" : "rejected") << '\n';
    for (const auto& diagnostic : result.diagnostics) {
        output << semantic::diagnostic_code_name(diagnostic.code) << '|'
               << diagnostic.message << '|' << span_snapshot(diagnostic.span) << '\n';
    }
    if (!result.program) return output.str();

    const auto& program = *result.program;
    output << "program|" << program.characterization_name << '|'
           << span_snapshot(program.span) << '|' << program.total_point_count << '\n';
    output << "reference|" << program.reference.symbol.value << '|'
           << program.reference.value.dbm() << '|' << span_snapshot(program.reference.span) << '\n';
    output << "frequency_sweep|" << program.sweep.symbol.value << '|'
           << program.sweep.start.hertz() << '|'
           << program.sweep.end.hertz() << '|'
           << program.sweep.step.hertz() << '|'
           << program.sweep.point_count << '|'
           << program.sweep.display_unit << '|'
           << span_snapshot(program.sweep.span) << '\n';
    for (const auto& sweep : program.sweeps) {
        std::visit(
            [&](const auto& value) {
                using Sweep = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Sweep, semantic::FrequencySweep>) {
                    output << "sweep_frequency|" << value.symbol.value << '|'
                           << value.start.hertz() << '|' << value.end.hertz() << '|'
                           << value.step.hertz();
                } else {
                    output << "sweep_temperature|" << value.symbol.value << '|'
                           << value.start.kelvin() << '|' << value.end.kelvin() << '|'
                           << value.step.kelvin();
                }
                output << '|' << value.point_count << '|' << value.display_unit << '|'
                       << span_snapshot(value.span) << '\n';
            },
            sweep);
    }
    output << "measurement|" << program.measurement.symbol.value << '|'
           << span_snapshot(program.measurement.span) << '\n';
    for (const auto& measurement : program.measurements) {
        output << "named_measurement|" << measurement.symbol.value << '|'
               << measurement.name << '|' << static_cast<int>(measurement.operation) << '|'
               << semantic::type_name(measurement.type) << '|'
               << (measurement.legacy ? "legacy" : "named") << '|'
               << span_snapshot(measurement.span) << '\n';
    }
    for (const auto& derived : program.derived_quantities) {
        output << "derived|" << derived.symbol.value << '|' << derived.name << '|'
               << semantic::type_name(derived.type) << '|'
               << semantic::type_name(derived.expression.type) << '|'
               << span_snapshot(derived.span) << '\n';
    }
    for (const auto& assertion : program.assertions) {
        output << "assertion|" << assertion.id << '|' << assertion.ordinal << '|'
               << semantic::type_name(assertion.expression.type) << '|'
               << span_snapshot(assertion.span) << '\n';
    }
    output << "calibration|" << program.calibration.symbol.value << '|'
           << program.calibration.name << '|'
           << semantic::type_name(program.calibration.correction.type) << '|'
           << semantic::type_name(program.calibration.type.correction_type) << '|';
    for (const auto index : program.calibration.indexes) output << index.value << ',';
    output << '|';
    for (const auto type : program.calibration.type.index_types) {
        output << semantic::type_name(type) << ',';
    }
    output << '|' << span_snapshot(program.calibration.span) << '\n';
    return output.str();
}

void require_semantic_equivalent(std::string_view case_name, std::string_view source) {
    const auto handwritten = semantic_snapshot(semantic::analyze(parser::parse(source)));
    const auto generated =
        semantic_snapshot(semantic::analyze(parser::antlr_frontend::parse(source)));
    if (handwritten == generated) return;
    throw std::runtime_error("semantic mismatch for " + std::string{case_name} +
                             "\nhandwritten:\n" + handwritten +
                             "generated:\n" + generated);
}

void shared_corpus_parity_test() {
    const Frontend handwritten{"handwritten", parser::lex, parser::parse};
    const Frontend generated{"antlr", parser::antlr_frontend::tokenize,
                             parser::antlr_frontend::parse};

    for (std::size_t index = 0; index < valid_sources.size(); ++index) {
        const auto name = "valid source " + std::to_string(index);
        require_equivalent(handwritten, generated, name, valid_sources[index]);
        require_semantic_equivalent(name, valid_sources[index]);
    }
    for (std::size_t index = 0; index < invalid_sources.size(); ++index) {
        require_equivalent(handwritten, generated,
                           "invalid source " + std::to_string(index), invalid_sources[index]);
    }
    for (const auto& example : example_cases) {
        const auto source = read_source(example.path);
        require_equivalent(handwritten, generated, example.path, source);
        require_semantic_equivalent(example.path, source);
    }
}

} // namespace

int main() {
    try {
        shared_corpus_parity_test();
        std::cout << "All ANTLR dual-parser parity tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
