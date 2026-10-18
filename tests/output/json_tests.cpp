#include "horusrf/output/json.hpp"

#include "test_support.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

#include "horusrf/device/simulated_rf_device.hpp"
#include "horusrf/ir/lowering.hpp"
#include "horusrf/parser/parser.hpp"
#include "horusrf/runtime/characterization.hpp"
#include "horusrf/semantic/analyzer.hpp"

namespace {
using namespace horusrf;

std::string read_fixture(const char* name) {
    std::ifstream input(std::string{HORUSRF_SOURCE_DIR} + "/examples/" + name,
                        std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

domain::CharacterizationResult execute(const char* name) {
    const auto analyzed = semantic::analyze(parser::parse(read_fixture(name)));
    CHECK(analyzed.ok());
    const auto lowered = ir::lower(*analyzed.program);
    CHECK(lowered.ok());
    device::SimulatedRfConnection connection;
    device::SimulatedRfTester tester{connection};
    device::SimulatedMeasurementDevice measurement{connection};
    device::SimulatedTemperatureController temperature{connection};
    auto execution = runtime::execute_characterization(
        *lowered.program,
        runtime::DeviceCapabilities{tester, &measurement, &temperature,
                                    &measurement, &measurement});
    CHECK(execution.ok());
    return std::move(*execution.result);
}

class CommaPunct : public std::numpunct<char> {
protected:
    char do_decimal_point() const override { return ','; }
};

void multidimensional_contract_test() {
    const auto result = execute("named_measurements.hrf");
    CHECK_EQ(result.characterization_name, std::string{"named_measurements"});
    CHECK_EQ(result.calibration.dimension_descriptors.size(), std::size_t{2});
    CHECK_EQ(result.calibration.entries.size(), result.samples.size());
    for (std::size_t i = 0; i < result.samples.size(); ++i) {
        CHECK_EQ(result.calibration.entries[i].operating_point,
                 result.samples[i].operating_point);
        CHECK_EQ(result.calibration.entries[i].correction,
                 result.samples[i].correction);
        CHECK_EQ(result.samples[i].derived_values.size(), std::size_t{1});
    }

    std::ostringstream output;
    const std::locale comma(std::locale::classic(), new CommaPunct);
    output.imbue(comma);
    output::write_json(output, result);
    const auto json = output.str();
    std::ostringstream repeated;
    output::write_json(repeated, result);
    CHECK_EQ(repeated.str(), json);
    CHECK(json.ends_with("\n"));
    CHECK(json.starts_with(
        "{\"schema\":{\"name\":\"horusrf.characterization-result\",\"version\":1}"));
    CHECK(json.find("\"characterization\":\"named_measurements\"") != std::string::npos);
    CHECK(json.find("\"dimensions\":[{\"name\":\"frequency\",\"type\":\"Frequency\"},"
                    "{\"name\":\"temperature\",\"type\":\"Temperature\"}]") !=
          std::string::npos);
    CHECK(json.find("\"name\":\"output_power\",\"operation\":\"power\","
                    "\"type\":\"Power\"") != std::string::npos);
    CHECK(json.find("\"name\":\"power_error\",\"type\":\"PowerDelta\"") !=
          std::string::npos);
    CHECK(json.find("\"canonical_unit\":\"K\",\"display_value\":-20,"
                    "\"display_unit\":\"degC\"") != std::string::npos);
    CHECK(json.find("\"validation\":null") != std::string::npos);
    CHECK(json.find("inf") == std::string::npos);
    CHECK(json.find("nan") == std::string::npos);
    CHECK(output.getloc() == comma);
}

void qualification_contract_test() {
    const auto result = execute("qualified_measurements.hrf");
    std::ostringstream output;
    output::write_json(output, result);
    const auto json = output.str();
    CHECK(json.find("\"status\":\"fail\"") != std::string::npos);
    CHECK(json.find("\"id\":\"assertion-1\"") != std::string::npos);
    CHECK(json.find("\"source\":{\"begin\":{\"line\":") != std::string::npos);
    CHECK(json.find("\"operator\":\"<=\"") != std::string::npos);
    CHECK(json.find("\"pass_ratio\":0") != std::string::npos);
}

void stable_type_names_test() {
    auto result = execute("tx_path_characterization.hrf");
    auto& values = result.samples.front().derived_values;
    values = {
        {"frequency_value", domain::ResultValueType::Frequency,
         domain::Frequency::from_hertz(1.0), "Hz"},
        {"frequency_delta", domain::ResultValueType::FrequencyDelta,
         domain::FrequencyDelta::from_hertz(2.0), "Hz"},
        {"power_value", domain::ResultValueType::Power,
         domain::Power::from_dbm(-3.0), "dBm"},
        {"power_delta", domain::ResultValueType::PowerDelta,
         domain::PowerDelta::from_db(4.0), "dB"},
        {"ratio_value", domain::ResultValueType::Ratio,
         domain::Ratio::from_ratio(0.05), "ratio"},
        {"temperature_value", domain::ResultValueType::Temperature,
         domain::Temperature::from_kelvin(300.0), "K"},
        {"temperature_delta", domain::ResultValueType::TemperatureDelta,
         domain::TemperatureDelta::from_kelvin(6.0), "K"},
        {"boolean_value", domain::ResultValueType::Boolean, true, "none"},
    };
    std::ostringstream output;
    output::write_json(output, result);
    const auto json = output.str();
    for (const auto* type : {"Frequency", "FrequencyDelta", "Power", "PowerDelta",
                             "Ratio", "Temperature", "TemperatureDelta", "Boolean"}) {
        CHECK(json.find(std::string{"\"type\":\""} + type + '"') !=
              std::string::npos);
    }
    CHECK(json.find("\"name\":\"boolean_value\",\"type\":\"Boolean\","
                    "\"value\":true}") != std::string::npos);
}

} // namespace

int main() {
    try {
        multidimensional_contract_test();
        qualification_contract_test();
        stable_type_names_test();
        std::cout << "All HorusRF JSON tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
