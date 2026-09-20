#pragma once

#include <optional>
#include <string_view>
#include <variant>

#include "horusrf/domain/quantity.hpp"

namespace horusrf::domain {

enum class Dimension { Frequency, AbsolutePower, RelativePower };
enum class Unit { Hz, KHz, MHz, GHz, DBm, DB };

[[nodiscard]] std::string_view spelling(Unit unit) noexcept;
[[nodiscard]] Dimension dimension(Unit unit) noexcept;
[[nodiscard]] double scale(Unit unit) noexcept;
[[nodiscard]] std::optional<Unit> parse_unit(std::string_view text) noexcept;

using CanonicalQuantity = std::variant<Frequency, Power, PowerDelta>;

enum class QuantityConversionError { InvalidNumber, NumberOutOfRange, UnknownUnit };

struct QuantityConversionResult {
    std::optional<CanonicalQuantity> value;
    std::optional<QuantityConversionError> error;
};

[[nodiscard]] QuantityConversionResult convert_quantity(std::string_view number,
                                                        std::string_view unit);

} // namespace horusrf::domain
