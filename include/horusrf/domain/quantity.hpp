#pragma once

#include <compare>

namespace horusrf::domain {

class Frequency {
public:
    static Frequency from_hertz(double value);
    [[nodiscard]] double hertz() const noexcept { return value_; }
    friend bool operator==(Frequency, Frequency) = default;
    friend auto operator<=>(Frequency, Frequency) = default;

private:
    explicit Frequency(double value) : value_(value) {}
    double value_;
};

class Power {
public:
    static Power from_dbm(double value);
    [[nodiscard]] double dbm() const noexcept { return value_; }
    friend bool operator==(Power, Power) = default;

private:
    explicit Power(double value) : value_(value) {}
    double value_;
};

class PowerDelta {
public:
    static PowerDelta from_db(double value);
    [[nodiscard]] double db() const noexcept { return value_; }
    friend bool operator==(PowerDelta, PowerDelta) = default;

private:
    explicit PowerDelta(double value) : value_(value) {}
    double value_;
};

[[nodiscard]] PowerDelta operator-(Power lhs, Power rhs);
[[nodiscard]] Power operator+(Power lhs, PowerDelta rhs);
[[nodiscard]] PowerDelta operator+(PowerDelta lhs, PowerDelta rhs);
[[nodiscard]] PowerDelta operator-(PowerDelta value);

} // namespace horusrf::domain
