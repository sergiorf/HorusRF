#include "horusrf/domain/units.hpp"

#include <charconv>
#include <cmath>
#include <system_error>

namespace horusrf::domain {

std::string_view spelling(Unit unit) noexcept {
    switch (unit) {
    case Unit::Hz: return "Hz";
    case Unit::KHz: return "kHz";
    case Unit::MHz: return "MHz";
    case Unit::GHz: return "GHz";
    case Unit::DBm: return "dBm";
    case Unit::DB: return "dB";
    }
    return {};
}

Dimension dimension(Unit unit) noexcept {
    switch (unit) {
    case Unit::Hz:
    case Unit::KHz:
    case Unit::MHz:
    case Unit::GHz: return Dimension::Frequency;
    case Unit::DBm: return Dimension::AbsolutePower;
    case Unit::DB: return Dimension::RelativePower;
    }
    return Dimension::Frequency;
}

double scale(Unit unit) noexcept {
    switch (unit) {
    case Unit::Hz: return 1.0;
    case Unit::KHz: return 1'000.0;
    case Unit::MHz: return 1'000'000.0;
    case Unit::GHz: return 1'000'000'000.0;
    case Unit::DBm:
    case Unit::DB: return 1.0;
    }
    return 1.0;
}

std::optional<Unit> parse_unit(std::string_view text) noexcept {
    if (text == "Hz") return Unit::Hz;
    if (text == "kHz") return Unit::KHz;
    if (text == "MHz") return Unit::MHz;
    if (text == "GHz") return Unit::GHz;
    if (text == "dBm") return Unit::DBm;
    if (text == "dB") return Unit::DB;
    return std::nullopt;
}

QuantityConversionResult convert_quantity(std::string_view number, std::string_view unit_text) {
    const auto unit = parse_unit(unit_text);
    if (!unit) return {{}, QuantityConversionError::UnknownUnit};

    double parsed = 0.0;
    const char* const begin = number.data();
    const char* const end = begin + number.size();
    const auto result = std::from_chars(begin, end, parsed, std::chars_format::general);
    if (result.ec == std::errc::result_out_of_range) {
        return {{}, QuantityConversionError::NumberOutOfRange};
    }
    if (result.ec != std::errc{} || result.ptr != end || number.empty()) {
        return {{}, QuantityConversionError::InvalidNumber};
    }

    const double canonical = parsed * scale(*unit);
    if (!std::isfinite(parsed) || !std::isfinite(canonical)) {
        return {{}, QuantityConversionError::NumberOutOfRange};
    }

    switch (dimension(*unit)) {
    case Dimension::Frequency:
        return {CanonicalQuantity{Frequency::from_hertz(canonical)}, {}};
    case Dimension::AbsolutePower:
        return {CanonicalQuantity{Power::from_dbm(canonical)}, {}};
    case Dimension::RelativePower:
        return {CanonicalQuantity{PowerDelta::from_db(canonical)}, {}};
    }
    return {{}, QuantityConversionError::UnknownUnit};
}

} // namespace horusrf::domain
