#include "horusrf/output/csv.hpp"

#include "test_support.hpp"

#include <array>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace horusrf;

constexpr auto header =
    "frequency_hz,reference_power_dbm,measured_power_dbm,error_db,correction_db,"
    "corrected_power_dbm,residual_error_db\n";

domain::CharacterizationSample sample(double frequency, double reference,
                                      double measured, double error,
                                      double correction, double corrected,
                                      double residual) {
    return {domain::Frequency::from_hertz(frequency),
            domain::Power::from_dbm(reference), domain::Power::from_dbm(measured),
            domain::PowerDelta::from_db(error),
            domain::PowerDelta::from_db(correction),
            domain::Power::from_dbm(corrected),
            domain::PowerDelta::from_db(residual)};
}

domain::CharacterizationResult fixture() {
    domain::CharacterizationResult result;
    result.samples = {
        sample(2.400000001e9, -10.25, -9.75, 0.5, -0.5, -10.25, 0.0),
        sample(123.125, 2.5, -3.75, -6.25, 6.125, 2.375, -0.125),
    };
    return result;
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> result;
    std::istringstream input(text);
    for (std::string line; std::getline(input, line);) result.push_back(line);
    return result;
}

std::vector<double> fields(const std::string& line) {
    std::vector<double> result;
    std::istringstream input(line);
    for (std::string field; std::getline(input, field, ',');) {
        result.push_back(std::stod(field));
    }
    return result;
}

class CommaPunct : public std::numpunct<char> {
protected:
    char do_decimal_point() const override { return ','; }
};

class FailingBuffer : public std::streambuf {
protected:
    std::streamsize xsputn(const char*, std::streamsize) override { return 0; }
    int_type overflow(int_type) override { return traits_type::eof(); }
};

void content_and_state_test() {
    std::ostringstream stream;
    const std::locale comma_locale(std::locale::classic(), new CommaPunct);
    stream.imbue(comma_locale);
    stream.setf(std::ios::scientific, std::ios::floatfield);
    stream.precision(3);
    const auto original_flags = stream.flags();
    const auto original_precision = stream.precision();
    const auto original_locale = stream.getloc();

    const auto result = fixture();
    output::write_csv(stream, result);
    const auto text = stream.str();
    CHECK(text.starts_with(header));
    CHECK(text.ends_with('\n'));
    CHECK_EQ(stream.flags(), original_flags);
    CHECK_EQ(stream.precision(), original_precision);
    CHECK(stream.getloc() == original_locale);

    const auto rows = lines(text);
    CHECK_EQ(rows.size(), std::size_t{3});
    CHECK_EQ(rows.front() + "\n", std::string{header});
    const std::array<const domain::CharacterizationSample*, 2> expected{
        &result.samples[0], &result.samples[1]};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const auto values = fields(rows[index + 1]);
        CHECK_EQ(values.size(), std::size_t{7});
        CHECK_EQ(values[0], expected[index]->frequency.hertz());
        CHECK_EQ(values[1], expected[index]->reference_power.dbm());
        CHECK_EQ(values[2], expected[index]->measured_power.dbm());
        CHECK_EQ(values[3], expected[index]->error.db());
        CHECK_EQ(values[4], expected[index]->correction.db());
        CHECK_EQ(values[5], expected[index]->corrected_power.dbm());
        CHECK_EQ(values[6], expected[index]->residual_error.db());
    }
}

void empty_and_failure_test() {
    std::ostringstream empty;
    output::write_csv(empty, domain::CharacterizationResult{});
    CHECK_EQ(empty.str(), std::string{header});

    FailingBuffer buffer;
    std::ostream failing(&buffer);
    try {
        output::write_csv(failing, fixture());
        CHECK(false);
    } catch (const std::ios_base::failure&) {
    }
}

} // namespace

int main() {
    try {
        content_and_state_test();
        empty_and_failure_test();
        std::cout << "All HorusRF CSV tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
