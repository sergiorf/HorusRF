#include "test_support.hpp"

#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "horusrf/parser/parser.hpp"

namespace {

std::string read_source(std::string_view relative_path) {
    const auto path = std::string{HORUSRF_SOURCE_DIR} + "/" + std::string{relative_path};
    std::ifstream input(path, std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

bool parses(std::string_view source) {
    try {
        (void)horusrf::parser::parse(source);
        return true;
    } catch (const horusrf::parser::ParseError&) {
        return false;
    }
}

} // namespace

void run_grammar_conformance_tests() {
    // Semantic-invalid examples remain syntactically valid and therefore belong
    // in this parser conformance set alongside the executable examples.
    constexpr std::array shipped_examples{
        "examples/tx_path_characterization.hrf",
        "examples/multidimensional_check.hrf",
        "examples/named_measurements.hrf",
        "examples/qualified_measurements.hrf",
        "examples/studio_default.hrf",
        "examples/studio_qualification.hrf",
        "examples/invalid/calibration_over_power.hrf",
        "examples/invalid/missing_calibration.hrf",
        "examples/invalid/power_plus_power.hrf",
        "examples/invalid/reference_uses_db.hrf",
        "examples/invalid/reversed_sweep.hrf",
        "examples/invalid/use_before_measurement.hrf",
    };
    for (const auto* path : shipped_examples) {
        CHECK(parses(read_source(path)));
    }

    constexpr std::array valid_sources{
        // Empty blocks and unknown operations are syntactically valid; semantic
        // analysis owns required declarations and operation resolution.
        "characterize empty {}",
        "characterize c { measure result = vendor_operation }",
        "characterize c { measure power = evm }",
        // Contextual expression names are accepted despite their keyword tokens.
        "characterize c { derive value = power + frequency - temperature }",
        "characterize c { derive value = power(frequency) }",
        "characterize c { derive calibration cal { correction = reference.power "
        "over frequency, temperature, power } }",
        // Every unit spelling and both supported sweep dimensions are concrete syntax.
        "characterize c { sweep frequency -1 Hz .. 2 kHz step 3 MHz "
        "sweep temperature -4 K .. 5 degC step 6 deltaK "
        "derive a = 7 GHz derive b = 8 dBm derive c = 9 dB "
        "derive d = 10 deltaC derive e = 11 % }",
        "// leading comment\r\ncharacterize c{// inline\nassert (1 dB + -2 dB) != 3 dB}",
    };
    for (const auto source : valid_sources) {
        CHECK(parses(source));
    }

    constexpr std::array invalid_sources{
        "",
        "characterize power {}",                  // keyword cannot name the block
        "characterize c {} characterize d {}",    // exactly one block
        "characterize c { reference power = 1 }", // quantities require units
        "characterize c { derive x = .5 dB }",    // leading-dot number
        "characterize c { derive x = 1. dB }",    // missing fractional digit
        "characterize c { derive x = 1e3 Hz }",   // scientific notation
        "characterize c { derive x = +1 dB }",    // no unary plus
        "characterize c { derive x = --a }",      // unary minus is not recursive
        "characterize c { derive x = a * b }",    // no multiplication
        "characterize c { assert a < b < c }",    // comparisons do not chain
        "characterize c { derive x = f() }",      // calls require one argument
        "characterize c { derive x = f(a, b) }",  // calls accept one argument
        "characterize c { sweep voltage 0 K .. 1 K step 1 K }",
        "characterize c { measure result }",      // named form requires '='
        "characterize c { derive frequency = 1 Hz }",
        "characterize c { derive calibration c { correction = 1 dB over } }",
        "characterize c { derive x = reference.measure }",
        "characterize c { derive x = 1Hz }",      // number/unit token boundary
        "characterize c { derive x = 1 dB; }",    // no statement terminator
    };
    for (const auto source : invalid_sources) {
        CHECK(!parses(source));
    }
}
