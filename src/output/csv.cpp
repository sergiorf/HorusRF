#include "horusrf/output/csv.hpp"

#include <ios>
#include <limits>
#include <locale>
#include <ostream>
#include <stdexcept>

namespace horusrf::output {
namespace {

class StreamState {
public:
    explicit StreamState(std::ostream& stream)
        : stream_(stream), flags_(stream.flags()), precision_(stream.precision()),
          locale_(stream.getloc()) {}

    ~StreamState() {
        try {
            stream_.imbue(locale_);
            stream_.flags(flags_);
            stream_.precision(precision_);
        } catch (...) {
        }
    }

private:
    std::ostream& stream_;
    std::ios_base::fmtflags flags_;
    std::streamsize precision_;
    std::locale locale_;
};

} // namespace

void write_csv(std::ostream& output,
               const domain::CharacterizationResult& result) {
    for (const auto& sample : result.samples) {
        const auto& coordinates = sample.operating_point.coordinates;
        if (!coordinates.empty() &&
            (coordinates.size() != 1 ||
             coordinates.front().type != domain::CoordinateType::Frequency ||
             !std::holds_alternative<domain::Frequency>(coordinates.front().value))) {
            throw std::invalid_argument(
                "legacy CSV cannot represent multidimensional operating points");
        }
    }

    StreamState state(output);
    output.imbue(std::locale::classic());
    output.precision(std::numeric_limits<double>::max_digits10);
    output << "frequency_hz,reference_power_dbm,measured_power_dbm,error_db,"
              "correction_db,corrected_power_dbm,residual_error_db\n";

    for (const auto& sample : result.samples) {
        output << sample.frequency.hertz() << ','
               << sample.reference_power.dbm() << ','
               << sample.measured_power.dbm() << ','
               << sample.error.db() << ','
               << sample.correction.db() << ','
               << sample.corrected_power.dbm() << ','
               << sample.residual_error.db() << '\n';
    }

    if (!output) {
        throw std::ios_base::failure("failed to write characterization CSV");
    }
}

} // namespace horusrf::output
