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

class FrequencyDelta {
public:
    static FrequencyDelta from_hertz(double value);
    [[nodiscard]] double hertz() const noexcept { return value_; }
    friend bool operator==(FrequencyDelta, FrequencyDelta) = default;
    friend auto operator<=>(FrequencyDelta, FrequencyDelta) = default;

private:
    explicit FrequencyDelta(double value) : value_(value) {}
    double value_;
};

class Ratio {
public:
    static Ratio from_ratio(double value);
    [[nodiscard]] double ratio() const noexcept { return value_; }
    friend bool operator==(Ratio, Ratio) = default;
    friend auto operator<=>(Ratio, Ratio) = default;

private:
    explicit Ratio(double value) : value_(value) {}
    double value_;
};

class TemperatureDelta;

class Temperature {
public:
    static Temperature from_kelvin(double value);
    [[nodiscard]] double kelvin() const noexcept { return value_; }
    friend bool operator==(Temperature, Temperature) = default;
    friend auto operator<=>(Temperature, Temperature) = default;

private:
    explicit Temperature(double value) : value_(value) {}
    double value_;
};

class TemperatureDelta {
public:
    static TemperatureDelta from_kelvin(double value);
    [[nodiscard]] double kelvin() const noexcept { return value_; }
    friend bool operator==(TemperatureDelta, TemperatureDelta) = default;
    friend auto operator<=>(TemperatureDelta, TemperatureDelta) = default;

private:
    explicit TemperatureDelta(double value) : value_(value) {}
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
[[nodiscard]] TemperatureDelta operator-(Temperature lhs, Temperature rhs);
[[nodiscard]] Temperature operator+(Temperature lhs, TemperatureDelta rhs);
[[nodiscard]] Temperature operator-(Temperature lhs, TemperatureDelta rhs);
[[nodiscard]] TemperatureDelta operator+(TemperatureDelta lhs, TemperatureDelta rhs);
[[nodiscard]] TemperatureDelta operator-(TemperatureDelta lhs, TemperatureDelta rhs);
[[nodiscard]] TemperatureDelta operator-(TemperatureDelta value);
[[nodiscard]] FrequencyDelta operator+(FrequencyDelta lhs, FrequencyDelta rhs);
[[nodiscard]] FrequencyDelta operator-(FrequencyDelta lhs, FrequencyDelta rhs);
[[nodiscard]] FrequencyDelta operator-(FrequencyDelta value);
[[nodiscard]] Ratio operator+(Ratio lhs, Ratio rhs);
[[nodiscard]] Ratio operator-(Ratio lhs, Ratio rhs);
[[nodiscard]] Ratio operator-(Ratio value);

} // namespace horusrf::domain
