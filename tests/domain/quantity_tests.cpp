#include "test_support.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <variant>

#include "horusrf/domain/units.hpp"

void run_results_tests();

namespace {
using namespace horusrf::domain;

template <typename L, typename R>
concept Addable = requires(L lhs, R rhs) { lhs + rhs; };
template <typename L, typename R>
concept Subtractable = requires(L lhs, R rhs) { lhs - rhs; };
template <typename T>
concept Negatable = requires(T value) { -value; };

static_assert(Addable<Power, PowerDelta>);
static_assert(Addable<PowerDelta, PowerDelta>);
static_assert(Subtractable<Power, Power>);
static_assert(Negatable<PowerDelta>);
static_assert(!Addable<Power, Power>);
static_assert(!Addable<PowerDelta, Power>);
static_assert(!Subtractable<PowerDelta, Power>);
static_assert(!Subtractable<Power, PowerDelta>);
static_assert(!Negatable<Power>);
static_assert(!Addable<Frequency, Frequency>);
static_assert(Subtractable<Temperature, Temperature>);
static_assert(Addable<Temperature, TemperatureDelta>);
static_assert(Subtractable<Temperature, TemperatureDelta>);
static_assert(Addable<TemperatureDelta, TemperatureDelta>);
static_assert(Subtractable<TemperatureDelta, TemperatureDelta>);
static_assert(Negatable<TemperatureDelta>);
static_assert(!Addable<TemperatureDelta, Temperature>);
static_assert(!Negatable<Temperature>);
static_assert(Addable<FrequencyDelta, FrequencyDelta>);
static_assert(Subtractable<Ratio, Ratio>);
static_assert(Negatable<Ratio>);

const CanonicalQuantity& converted(std::string_view number, std::string_view unit) {
    static QuantityConversionResult result;
    result = convert_quantity(number, unit);
    CHECK(result.value.has_value());
    CHECK(!result.error.has_value());
    return *result.value;
}

void conversion_tests() {
    CHECK_NEAR(std::get<Frequency>(converted("2.5", "Hz")).hertz(), 2.5, 1e-12);
    CHECK_NEAR(std::get<Frequency>(converted("2.5", "kHz")).hertz(), 2.5e3, 1e-9);
    CHECK_NEAR(std::get<Frequency>(converted("2.5", "MHz")).hertz(), 2.5e6, 1e-6);
    CHECK_NEAR(std::get<Frequency>(converted("2.5", "GHz")).hertz(), 2.5e9, 1e-3);
    CHECK_NEAR(std::get<Power>(converted("-10", "dBm")).dbm(), -10.0, 1e-12);
    CHECK_NEAR(std::get<PowerDelta>(converted("3.25", "dB")).db(), 3.25, 1e-12);
    CHECK_NEAR(std::get<Ratio>(converted("5", "%")).ratio(), 0.05, 1e-12);
    CHECK_NEAR(std::get<Temperature>(converted("0", "degC")).kelvin(), 273.15, 1e-12);
    CHECK_NEAR(std::get<Temperature>(converted("20", "K")).kelvin(), 20.0, 1e-12);
    CHECK_NEAR(std::get<TemperatureDelta>(converted("20", "deltaC")).kelvin(),
               20.0, 1e-12);
    CHECK_NEAR(std::get<TemperatureDelta>(converted("20", "deltaK")).kelvin(),
               20.0, 1e-12);
    CHECK_EQ(*convert_quantity("-273.16", "degC").error,
             QuantityConversionError::BelowAbsoluteZero);

    CHECK_EQ(*convert_quantity("1", "watts").error, QuantityConversionError::UnknownUnit);
    CHECK_EQ(*convert_quantity("abc", "Hz").error, QuantityConversionError::InvalidNumber);
    CHECK_EQ(*convert_quantity("1x", "Hz").error, QuantityConversionError::InvalidNumber);
    CHECK_EQ(*convert_quantity("1e9999", "Hz").error,
             QuantityConversionError::NumberOutOfRange);
    CHECK_EQ(*convert_quantity("nan", "Hz").error,
             QuantityConversionError::NumberOutOfRange);
}

void arithmetic_tests() {
    const auto high = Power::from_dbm(10.0);
    const auto low = Power::from_dbm(-5.0);
    CHECK_NEAR((high - low).db(), 15.0, 1e-12);
    CHECK_NEAR((low + PowerDelta::from_db(3.0)).dbm(), -2.0, 1e-12);
    CHECK_NEAR((PowerDelta::from_db(2.0) + PowerDelta::from_db(4.0)).db(), 6.0, 1e-12);
    CHECK_NEAR((-PowerDelta::from_db(2.0)).db(), -2.0, 1e-12);
    CHECK(Frequency::from_hertz(10.0) > Frequency::from_hertz(9.0));
    const auto room = Temperature::from_kelvin(293.15);
    const auto freezing = Temperature::from_kelvin(273.15);
    CHECK_NEAR((room - freezing).kelvin(), 20.0, 1e-12);
    CHECK_NEAR((freezing + TemperatureDelta::from_kelvin(20.0)).kelvin(),
               293.15, 1e-12);
    CHECK_NEAR((FrequencyDelta::from_hertz(20.0) -
                FrequencyDelta::from_hertz(5.0)).hertz(), 15.0, 1e-12);
    CHECK_NEAR((Ratio::from_ratio(0.05) + Ratio::from_ratio(0.02)).ratio(),
               0.07, 1e-12);

    bool threw = false;
    try {
        (void)Power::from_dbm(std::numeric_limits<double>::infinity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}
} // namespace

int main() {
    try {
        conversion_tests();
        arithmetic_tests();
        run_results_tests();
        std::cout << "All HorusRF domain tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
