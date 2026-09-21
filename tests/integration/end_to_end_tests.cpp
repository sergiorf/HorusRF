#include "test_support.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "horusrf/device/simulated_rf_device.hpp"
#include "horusrf/ir/lowering.hpp"
#include "horusrf/output/csv.hpp"
#include "horusrf/parser/parser.hpp"
#include "horusrf/runtime/characterization.hpp"
#include "horusrf/semantic/analyzer.hpp"

namespace {
using namespace horusrf;

std::string fixture() {
    std::ifstream input(std::string{HORUSRF_SOURCE_DIR} +
                        "/examples/tx_path_characterization.hrf", std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void run_test() {
    const auto syntax = parser::parse(fixture());
    const auto analyzed = semantic::analyze(syntax);
    CHECK(analyzed.ok());
    const auto& semantic_program = *analyzed.program;
    CHECK_EQ(semantic_program.characterization_name, std::string{"tx_path"});
    CHECK_NEAR(semantic_program.reference.value.dbm(), -10.0, 1e-12);
    CHECK_NEAR(semantic_program.sweep.start.hertz(), 2.40e9, 1e-3);
    CHECK_NEAR(semantic_program.sweep.end.hertz(), 2.50e9, 1e-3);
    CHECK_NEAR(semantic_program.sweep.step.hertz(), 1.0e6, 1e-3);
    CHECK_EQ(semantic_program.calibrations.size(), std::size_t{1});
    CHECK_EQ(semantic_program.calibrations[0].name, std::string{"tx_power"});
    CHECK_EQ(semantic_program.calibrations[0].indexes.size(), std::size_t{1});
    CHECK_EQ(semantic_program.calibrations[0].indexes[0], semantic_program.sweep.symbol);

    const auto lowered = ir::lower(semantic_program);
    CHECK(lowered.ok());
    CHECK_EQ(lowered.program->calibrations[0].indexes[0],
             lowered.program->sweep.frequency);

    device::SimulatedRfDevice simulator;
    const auto execution = runtime::execute_characterization(*lowered.program, simulator);
    CHECK(execution.ok());
    const auto& result = *execution.result;
    CHECK_EQ(result.samples.size(), std::size_t{101});
    const std::array<std::size_t, 5> indexes{0, 25, 50, 75, 100};
    const std::array<double, 5> errors{0.35, 0.55, 0.35, 0.15, 0.35};
    for (std::size_t index = 0; index < result.samples.size(); ++index) {
        const auto& sample = result.samples[index];
        CHECK_NEAR(sample.frequency.hertz(), 2.40e9 + index * 1.0e6, 1e-3);
        CHECK_NEAR(sample.reference_power.dbm(), -10.0, 1e-12);
        CHECK_NEAR(sample.error.db(),
                   sample.measured_power.dbm() - sample.reference_power.dbm(), 1e-12);
        CHECK_NEAR(sample.correction.db(),
                   sample.reference_power.dbm() - sample.measured_power.dbm(), 1e-12);
        CHECK_NEAR(sample.corrected_power.dbm(),
                   sample.measured_power.dbm() + sample.correction.db(), 1e-12);
        CHECK_NEAR(sample.residual_error.db(),
                   sample.corrected_power.dbm() - sample.reference_power.dbm(), 1e-12);
        CHECK_EQ(result.calibration.dimensions[index], sample.frequency);
        CHECK_EQ(result.calibration.corrections[index], sample.correction);
    }
    for (std::size_t index = 0; index < indexes.size(); ++index) {
        CHECK_NEAR(result.samples[indexes[index]].error.db(), errors[index], 1e-12);
    }
    CHECK_EQ(result.calibration.name, std::string{"tx_power"});
    CHECK_EQ(result.calibration.dimensions.size(), result.samples.size());
    CHECK_EQ(result.calibration.corrections.size(), result.samples.size());
    CHECK(result.metrics.corrected_rms_error_db < 1e-12);
    CHECK(result.metrics.corrected_maximum_absolute_error_db < 1e-12);
    CHECK(result.metrics.corrected_rms_error_db <
          0.1 * result.metrics.uncorrected_rms_error_db);

    std::ostringstream csv;
    output::write_csv(csv, result);
    std::vector<std::string> lines;
    std::istringstream csv_input(csv.str());
    for (std::string line; std::getline(csv_input, line);) lines.push_back(line);
    CHECK_EQ(lines.size(), std::size_t{102});
    CHECK_EQ(lines.front(), std::string{"frequency_hz,reference_power_dbm,"
        "measured_power_dbm,error_db,correction_db,corrected_power_dbm,residual_error_db"});
    CHECK(lines[1].starts_with("2400000000,-10,"));
    CHECK(lines.back().starts_with("2500000000,-10,"));
}

} // namespace

int main() {
    try {
        run_test();
        std::cout << "HorusRF end-to-end test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
