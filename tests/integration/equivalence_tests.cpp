#include "test_support.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>

#include "horusrf/device/simulated_rf_device.hpp"
#include "horusrf/ir/lowering.hpp"
#include "horusrf/parser/parser.hpp"
#include "horusrf/runtime/characterization.hpp"
#include "horusrf/semantic/analyzer.hpp"
#include "tx_path_characterization.hpp"

namespace {
using namespace horusrf;
constexpr double frequency_tolerance = 1e-3;
constexpr double value_tolerance = 1e-12;

void require_near(double declarative, double procedural, double tolerance,
                  std::string_view field, std::size_t index) {
    if (declarative == procedural) return;
    if (std::abs(declarative - procedural) <= tolerance) return;
    std::ostringstream message;
    message << "declarative/procedural mismatch for " << field
            << " at sample " << index << ": " << declarative << " vs " << procedural;
    throw std::runtime_error(message.str());
}

domain::CharacterizationResult declarative_result(device::RfDevice& device) {
    std::ifstream input(std::string{HORUSRF_SOURCE_DIR} +
                        "/examples/tx_path_characterization.hrf", std::ios::binary);
    CHECK(input.good());
    const std::string source{std::istreambuf_iterator<char>{input},
                             std::istreambuf_iterator<char>{}};
    const auto syntax = parser::parse(source);
    const auto analyzed = semantic::analyze(syntax);
    CHECK(analyzed.ok());
    const auto lowered = ir::lower(*analyzed.program);
    CHECK(lowered.ok());
    auto execution = runtime::execute_characterization(*lowered.program, device);
    CHECK(execution.ok());
    return std::move(*execution.result);
}

void run_test() {
    device::SimulatedRfDevice procedural_device;
    device::SimulatedRfDevice declarative_device;
    const auto procedural = examples::run_tx_path_characterization(procedural_device);
    const auto declarative = declarative_result(declarative_device);

    CHECK_EQ(declarative.samples.size(), procedural.samples.size());
    CHECK_EQ(declarative.calibration.name, procedural.calibration.name);
    CHECK_EQ(declarative.calibration.dimensions.size(),
             procedural.calibration.dimensions.size());
    CHECK_EQ(declarative.calibration.corrections.size(),
             procedural.calibration.corrections.size());
    for (std::size_t index = 0; index < declarative.samples.size(); ++index) {
        const auto& left = declarative.samples[index];
        const auto& right = procedural.samples[index];
        require_near(left.frequency.hertz(), right.frequency.hertz(),
                     frequency_tolerance, "frequency", index);
        require_near(left.reference_power.dbm(), right.reference_power.dbm(),
                     value_tolerance, "reference power", index);
        require_near(left.measured_power.dbm(), right.measured_power.dbm(),
                     value_tolerance, "measured power", index);
        require_near(left.error.db(), right.error.db(), value_tolerance, "error", index);
        require_near(left.correction.db(), right.correction.db(), value_tolerance,
                     "correction", index);
        require_near(left.corrected_power.dbm(), right.corrected_power.dbm(),
                     value_tolerance, "corrected power", index);
        require_near(left.residual_error.db(), right.residual_error.db(),
                     value_tolerance, "residual error", index);
        require_near(declarative.calibration.dimensions[index].hertz(),
                     procedural.calibration.dimensions[index].hertz(),
                     frequency_tolerance, "artifact dimension", index);
        require_near(declarative.calibration.corrections[index].db(),
                     procedural.calibration.corrections[index].db(),
                     value_tolerance, "artifact correction", index);
    }
    const auto& left = declarative.metrics;
    const auto& right = procedural.metrics;
    require_near(left.uncorrected_rms_error_db, right.uncorrected_rms_error_db,
                 value_tolerance, "uncorrected RMS metric", 0);
    require_near(left.corrected_rms_error_db, right.corrected_rms_error_db,
                 value_tolerance, "corrected RMS metric", 0);
    require_near(left.maximum_absolute_error_db, right.maximum_absolute_error_db,
                 value_tolerance, "maximum error metric", 0);
    require_near(left.corrected_maximum_absolute_error_db,
                 right.corrected_maximum_absolute_error_db, value_tolerance,
                 "corrected maximum error metric", 0);
    require_near(left.rms_improvement_ratio, right.rms_improvement_ratio,
                 value_tolerance, "RMS improvement metric", 0);
}

} // namespace

int main() {
    try {
        run_test();
        std::cout << "HorusRF equivalence test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
