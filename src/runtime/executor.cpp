#include "horusrf/runtime/executor.hpp"

#include "executor_internal.hpp"

#include <cstddef>
#include <cmath>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace horusrf::runtime {

std::string_view diagnostic_code_name(DiagnosticCode code) noexcept {
    switch (code) {
    case DiagnosticCode::InvalidValueId: return "runtime.invalid_value_id";
    case DiagnosticCode::InvalidDefinitionOrder: return "runtime.invalid_definition_order";
    case DiagnosticCode::TypeMismatch: return "runtime.type_mismatch";
    case DiagnosticCode::InvalidProgramBinding: return "runtime.invalid_program_binding";
    case DiagnosticCode::InvalidOperation: return "runtime.invalid_operation";
    case DiagnosticCode::InvalidSweep: return "runtime.invalid_sweep";
    case DiagnosticCode::SweepTooLarge: return "runtime.sweep_too_large";
    case DiagnosticCode::UnsupportedSweepShape:
        return "runtime.unsupported_sweep_shape";
    case DiagnosticCode::CapabilityUnavailable:
        return "runtime.capability_unavailable";
    case DiagnosticCode::InvalidCalibrationBinding:
        return "runtime.invalid_calibration_binding";
    case DiagnosticCode::NonFiniteValue: return "runtime.non_finite_value";
    }
    return "runtime.unknown";
}

namespace {

template <typename... Visitors>
struct Overloaded : Visitors... {
    using Visitors::operator()...;
};
template <typename... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

class Validator {
public:
    explicit Validator(const ir::Program& program) : program_(program) {}

    std::vector<Diagnostic> run() {
        for (std::size_t index = 0; index < program_.values.size(); ++index) {
            const auto& value = program_.values[index];
            if (value.id.value != index) {
                return failure(DiagnosticCode::InvalidValueId,
                               "value ID does not match its dense slot", value.span);
            }
            if (!validate_definition(value, index)) return std::move(diagnostics_);
        }
        if (!validate_base_bindings()) return std::move(diagnostics_);
        if (!validate_named_values()) return std::move(diagnostics_);
        if (!validate_assertions()) return std::move(diagnostics_);
        validate_calibration();
        return std::move(diagnostics_);
    }

private:
    bool report(DiagnosticCode code, std::string message, parser::SourceSpan span) {
        diagnostics_.push_back(Diagnostic{code, std::move(message), span});
        return false;
    }

    std::vector<Diagnostic> failure(DiagnosticCode code, std::string message,
                                    parser::SourceSpan span) {
        report(code, std::move(message), span);
        return std::move(diagnostics_);
    }

    bool operand(ir::ValueId id, std::size_t consumer, ir::ValueType expected,
                 parser::SourceSpan span) {
        if (id.value >= program_.values.size()) {
            return report(DiagnosticCode::InvalidValueId,
                          "operation operand has an invalid value ID", span);
        }
        if (id.value >= consumer) {
            return report(DiagnosticCode::InvalidDefinitionOrder,
                          "operation operand does not precede its consumer", span);
        }
        if (program_.values[id.value].type != expected) {
            return report(DiagnosticCode::TypeMismatch,
                          "operation operand has the wrong value type", span);
        }
        return true;
    }

    bool validate_definition(const ir::Value& value, std::size_t index) {
        return std::visit(
            Overloaded{
                [&](const ir::Constant& operation) {
                    const auto actual = std::visit(
                        Overloaded{
                            [](domain::Frequency) { return ir::ValueType::Frequency; },
                            [](domain::FrequencyDelta) { return ir::ValueType::FrequencyDelta; },
                            [](domain::Ratio) { return ir::ValueType::Ratio; },
                            [](domain::Temperature) { return ir::ValueType::Temperature; },
                            [](domain::TemperatureDelta) {
                                return ir::ValueType::TemperatureDelta;
                            },
                            [](domain::Power) { return ir::ValueType::Power; },
                            [](domain::PowerDelta) { return ir::ValueType::PowerDelta; },
                            [](bool) { return ir::ValueType::Boolean; }},
                        operation.value);
                    return actual == value.type ||
                           report(DiagnosticCode::TypeMismatch,
                                  "constant value does not match its declared type", value.span);
                },
                [&](const ir::ReferencePower&) {
                    return value.type == ir::ValueType::Power ||
                           report(DiagnosticCode::TypeMismatch,
                                  "reference power definition must have type Power", value.span);
                },
                [&](const ir::SweepFrequency&) {
                    return value.type == ir::ValueType::Frequency ||
                           report(DiagnosticCode::TypeMismatch,
                                  "sweep frequency definition must have type Frequency",
                                  value.span);
                },
                [&](const ir::SweepTemperature&) {
                    return value.type == ir::ValueType::Temperature ||
                           report(DiagnosticCode::TypeMismatch,
                                  "sweep temperature definition must have type Temperature",
                                  value.span);
                },
                [&](const ir::MeasurePower&) {
                    return value.type == ir::ValueType::Power ||
                           report(DiagnosticCode::TypeMismatch,
                                  "measurement definition must have type Power", value.span);
                },
                [&](const ir::MeasureEvm&) {
                    return value.type == ir::ValueType::Ratio ||
                           report(DiagnosticCode::TypeMismatch,
                                  "EVM measurement must have type Ratio", value.span);
                },
                [&](const ir::MeasureFrequencyError&) {
                    return value.type == ir::ValueType::FrequencyDelta ||
                           report(DiagnosticCode::TypeMismatch,
                                  "frequency-error measurement must have type FrequencyDelta",
                                  value.span);
                },
                [&](const ir::NegateDelta& operation) {
                    if (value.type != ir::ValueType::FrequencyDelta &&
                        value.type != ir::ValueType::Ratio &&
                        value.type != ir::ValueType::TemperatureDelta) {
                        return report(DiagnosticCode::TypeMismatch,
                                      "delta negation has an invalid type", value.span);
                    }
                    return operand(operation.operand, index, value.type, value.span);
                },
                [&](const ir::AddDelta& operation) {
                    if (value.type != ir::ValueType::FrequencyDelta &&
                        value.type != ir::ValueType::Ratio &&
                        value.type != ir::ValueType::TemperatureDelta) {
                        return report(DiagnosticCode::TypeMismatch,
                                      "delta addition has an invalid type", value.span);
                    }
                    return operand(operation.left, index, value.type, value.span) &&
                           operand(operation.right, index, value.type, value.span);
                },
                [&](const ir::SubtractDelta& operation) {
                    if (value.type != ir::ValueType::FrequencyDelta &&
                        value.type != ir::ValueType::Ratio &&
                        value.type != ir::ValueType::TemperatureDelta) {
                        return report(DiagnosticCode::TypeMismatch,
                                      "delta subtraction has an invalid type", value.span);
                    }
                    return operand(operation.left, index, value.type, value.span) &&
                           operand(operation.right, index, value.type, value.span);
                },
                [&](const ir::Alias& operation) {
                    if (operation.value.value >= program_.values.size()) {
                        return report(DiagnosticCode::InvalidValueId,
                                      "alias has an invalid value ID", value.span);
                    }
                    if (operation.value.value >= index) {
                        return report(DiagnosticCode::InvalidDefinitionOrder,
                                      "alias operand does not precede its consumer", value.span);
                    }
                    return program_.values[operation.value.value].type == value.type ||
                           report(DiagnosticCode::TypeMismatch,
                                  "alias does not preserve its operand type", value.span);
                },
                [&](const ir::NegatePowerDelta& operation) {
                    return value.type == ir::ValueType::PowerDelta &&
                                   operand(operation.operand, index,
                                           ir::ValueType::PowerDelta, value.span) ||
                           (!diagnostics_.empty() ? false
                                                 : report(DiagnosticCode::TypeMismatch,
                                                          "delta negation must produce PowerDelta",
                                                          value.span));
                },
                [&](const ir::AddPowerDelta& operation) {
                    if (value.type != ir::ValueType::PowerDelta) {
                        return report(DiagnosticCode::TypeMismatch,
                                      "delta addition must produce PowerDelta", value.span);
                    }
                    return operand(operation.left, index, ir::ValueType::PowerDelta,
                                   value.span) &&
                           operand(operation.right, index, ir::ValueType::PowerDelta,
                                   value.span);
                },
                [&](const ir::ApplyPowerDelta& operation) {
                    if (value.type != ir::ValueType::Power) {
                        return report(DiagnosticCode::TypeMismatch,
                                      "applying a delta must produce Power", value.span);
                    }
                    return operand(operation.power, index, ir::ValueType::Power, value.span) &&
                           operand(operation.delta, index, ir::ValueType::PowerDelta,
                                   value.span);
                },
                [&](const ir::PowerDifference& operation) {
                    if (value.type != ir::ValueType::PowerDelta) {
                        return report(DiagnosticCode::TypeMismatch,
                                      "power difference must produce PowerDelta", value.span);
                    }
                    return operand(operation.left, index, ir::ValueType::Power, value.span) &&
                           operand(operation.right, index, ir::ValueType::Power, value.span);
                },
                [&](const ir::AbsoluteValue& operation) {
                    if (value.type != ir::ValueType::PowerDelta &&
                        value.type != ir::ValueType::FrequencyDelta &&
                        value.type != ir::ValueType::Ratio &&
                        value.type != ir::ValueType::TemperatureDelta) {
                        return report(DiagnosticCode::TypeMismatch,
                                      "abs() has an invalid value type", value.span);
                    }
                    return operand(operation.operand, index, value.type, value.span);
                },
                [&](const ir::Compare& operation) {
                    if (value.type != ir::ValueType::Boolean ||
                        operation.operand_type == ir::ValueType::Boolean) {
                        return report(DiagnosticCode::TypeMismatch,
                                      "comparison must produce Boolean", value.span);
                    }
                    return operand(operation.left, index, operation.operand_type, value.span) &&
                           operand(operation.right, index, operation.operand_type, value.span);
                }},
            value.definition);
    }

    bool binding(ir::ValueId id, ir::ValueType type, std::size_t definition_index,
                 parser::SourceSpan span, const char* name) {
        if (id.value >= program_.values.size()) {
            return report(DiagnosticCode::InvalidValueId,
                          std::string{name} + " binding has an invalid value ID", span);
        }
        const auto& value = program_.values[id.value];
        if (value.type != type || value.definition.index() != definition_index) {
            return report(DiagnosticCode::InvalidProgramBinding,
                          std::string{name} + " binding identifies the wrong definition", span);
        }
        return true;
    }

    bool validate_base_bindings() {
        std::size_t references{};
        std::size_t sweeps{};
        std::size_t measurements{};
        for (const auto& value : program_.values) {
            references += std::holds_alternative<ir::ReferencePower>(value.definition) ? 1U : 0U;
            sweeps += (std::holds_alternative<ir::SweepFrequency>(value.definition) ||
                       std::holds_alternative<ir::SweepTemperature>(value.definition)) ? 1U : 0U;
            measurements += (std::holds_alternative<ir::MeasurePower>(value.definition) ||
                             std::holds_alternative<ir::MeasureEvm>(value.definition) ||
                             std::holds_alternative<ir::MeasureFrequencyError>(value.definition))
                                ? 1U : 0U;
        }
        if (references != 1 || sweeps != program_.sweeps.size() || sweeps == 0 ||
            measurements != program_.measurements.size() || measurements == 0) {
            return report(DiagnosticCode::InvalidProgramBinding,
                          "program must contain one reference and measurement and its declared sweeps",
                          program_.span);
        }
        if (!binding(program_.reference, ir::ValueType::Power,
                       ir::Definition{ir::ReferencePower{
                           domain::Power::from_dbm(0.0)}}.index(),
                       program_.span, "reference")) return false;
        std::size_t product = 1;
        std::size_t frequency_dimensions = 0;
        std::size_t temperature_dimensions = 0;
        const ir::TypedSweep* frequency_sweep = nullptr;
        for (const auto& sweep : program_.sweeps) {
            const auto expected_type = sweep.dimension == ir::SweepDimension::Frequency
                                           ? ir::ValueType::Frequency
                                           : ir::ValueType::Temperature;
            const auto definition_index = sweep.dimension == ir::SweepDimension::Frequency
                                              ? ir::Definition{ir::SweepFrequency{}}.index()
                                              : ir::Definition{ir::SweepTemperature{}}.index();
            if (!binding(sweep.value, expected_type, definition_index, sweep.span,
                         "sweep")) return false;
            if (sweep.dimension == ir::SweepDimension::Frequency) {
                ++frequency_dimensions;
                frequency_sweep = &sweep;
                if (sweep.name != "frequency" ||
                    !std::holds_alternative<domain::Frequency>(sweep.start) ||
                    !std::holds_alternative<domain::Frequency>(sweep.end) ||
                    !std::holds_alternative<domain::Frequency>(sweep.step)) {
                    return report(DiagnosticCode::InvalidProgramBinding,
                                  "frequency sweep metadata has inconsistent types",
                                  sweep.span);
                }
            } else {
                ++temperature_dimensions;
                if (sweep.name != "temperature" ||
                    !std::holds_alternative<domain::Temperature>(sweep.start) ||
                    !std::holds_alternative<domain::Temperature>(sweep.end) ||
                    !std::holds_alternative<domain::TemperatureDelta>(sweep.step)) {
                    return report(DiagnosticCode::InvalidProgramBinding,
                                  "temperature sweep metadata has inconsistent types",
                                  sweep.span);
                }
            }
            if (sweep.point_count == 0 || product > 1'000'000U / sweep.point_count) {
                return report(DiagnosticCode::SweepTooLarge,
                              "sweep product exceeds the safety limit", sweep.span);
            }
            product *= sweep.point_count;
        }
        if (product != program_.total_point_count) {
            return report(DiagnosticCode::InvalidProgramBinding,
                          "sweep product metadata is inconsistent", program_.span);
        }
        if (frequency_dimensions != 1 || temperature_dimensions > 1 ||
            frequency_sweep == nullptr ||
            frequency_sweep->value != program_.sweep.frequency ||
            !binding(program_.sweep.frequency, ir::ValueType::Frequency,
                       ir::Definition{ir::SweepFrequency{}}.index(), program_.sweep.span,
                       "legacy sweep")) return false;
        if (!binding(program_.measurement, ir::ValueType::Power,
                       ir::Definition{ir::MeasurePower{}}.index(), program_.span,
                       "measurement")) return false;
        for (const auto& measurement : program_.measurements) {
            if (measurement.name.empty() || measurement.value.value >= program_.values.size() ||
                program_.values[measurement.value.value].type != measurement.type) {
                return report(DiagnosticCode::InvalidProgramBinding,
                              "measurement metadata is inconsistent", measurement.span);
            }
            const auto& definition = program_.values[measurement.value.value].definition;
            const bool matches =
                (measurement.operation == ir::MeasurementOperation::Power &&
                 measurement.type == ir::ValueType::Power &&
                 std::holds_alternative<ir::MeasurePower>(definition)) ||
                (measurement.operation == ir::MeasurementOperation::Evm &&
                 measurement.type == ir::ValueType::Ratio &&
                 std::holds_alternative<ir::MeasureEvm>(definition)) ||
                (measurement.operation == ir::MeasurementOperation::FrequencyError &&
                 measurement.type == ir::ValueType::FrequencyDelta &&
                 std::holds_alternative<ir::MeasureFrequencyError>(definition));
            if (!matches) return report(DiagnosticCode::InvalidProgramBinding,
                                        "measurement operation metadata is inconsistent",
                                        measurement.span);
        }
        return true;
    }

    bool validate_named_values() {
        for (const auto& named : program_.named_values) {
            if (named.value.value >= program_.values.size()) {
                return report(DiagnosticCode::InvalidValueId,
                              "named value has an invalid value ID", named.span);
            }
            if (program_.values[named.value.value].type != named.type) {
                return report(DiagnosticCode::TypeMismatch,
                              "named value metadata does not match its value", named.span);
            }
        }
        return true;
    }

    bool validate_assertions() {
        for (std::size_t index = 0; index < program_.assertions.size(); ++index) {
            const auto& assertion = program_.assertions[index];
            if (assertion.id != "assertion-" + std::to_string(index + 1) ||
                assertion.ordinal != index + 1 ||
                assertion.value.value >= program_.values.size()) {
                return report(DiagnosticCode::InvalidProgramBinding,
                              "assertion metadata is inconsistent", assertion.span);
            }
            const auto& value = program_.values[assertion.value.value];
            auto comparison_value = assertion.value;
            const ir::Compare* comparison = nullptr;
            while (comparison_value.value < program_.values.size()) {
                const auto& definition = program_.values[comparison_value.value].definition;
                comparison = std::get_if<ir::Compare>(&definition);
                if (comparison) break;
                const auto* alias = std::get_if<ir::Alias>(&definition);
                if (!alias || alias->value.value >= comparison_value.value) break;
                comparison_value = alias->value;
            }
            if (value.type != ir::ValueType::Boolean || comparison == nullptr ||
                comparison->left != assertion.left || comparison->right != assertion.right ||
                comparison->operand_type != assertion.operand_type ||
                comparison->op != assertion.op) {
                return report(DiagnosticCode::InvalidProgramBinding,
                              "assertion does not match its comparison value", assertion.span);
            }
        }
        return true;
    }

    bool validate_calibration() {
        const auto& calibration = program_.calibration;
        if (calibration.correction.value >= program_.values.size()) {
            return report(DiagnosticCode::InvalidValueId,
                          "calibration correction has an invalid value ID",
                          calibration.span);
        }
        if (calibration.correction_type != ir::ValueType::PowerDelta ||
            program_.values[calibration.correction.value].type !=
                ir::ValueType::PowerDelta) {
            return report(DiagnosticCode::TypeMismatch,
                          "calibration correction must have type PowerDelta",
                          calibration.span);
        }
        if (calibration.indexes.size() != program_.sweeps.size() ||
            calibration.index_types.size() != program_.sweeps.size()) {
            return report(DiagnosticCode::InvalidProgramBinding,
                          "calibration must be indexed by every active sweep",
                          calibration.span);
        }
        for (std::size_t i = 0; i < program_.sweeps.size(); ++i) {
            const auto expected_type = program_.sweeps[i].dimension ==
                                               ir::SweepDimension::Frequency
                                           ? ir::ValueType::Frequency
                                           : ir::ValueType::Temperature;
            if (calibration.indexes[i] != program_.sweeps[i].value ||
                calibration.index_types[i] != expected_type) {
                return report(DiagnosticCode::InvalidProgramBinding,
                              "calibration sweep indexes are not in declaration order",
                              calibration.span);
            }
        }
        return true;
    }

    const ir::Program& program_;
    std::vector<Diagnostic> diagnostics_;
};

template <typename T>
const T& slot(const std::vector<RuntimeValue>& values, ir::ValueId id) {
    return std::get<T>(values[id.value]);
}

RuntimeValue evaluate_definition(const ir::Definition& definition,
                                 const domain::OperatingPoint& point,
                                 DeviceCapabilities devices,
                                 const std::vector<RuntimeValue>& values) {
    return std::visit(
        Overloaded{
            [](const ir::Constant& operation) -> RuntimeValue {
                return std::visit([](const auto& value) -> RuntimeValue { return value; },
                                  operation.value);
            },
            [](const ir::ReferencePower& operation) -> RuntimeValue { return operation.value; },
            [&](const ir::SweepFrequency&) -> RuntimeValue {
                for (const auto& coordinate : point.coordinates) {
                    if (coordinate.type == domain::CoordinateType::Frequency) {
                        return std::get<domain::Frequency>(coordinate.value);
                    }
                }
                throw std::logic_error("operating point has no frequency coordinate");
            },
            [&](const ir::SweepTemperature&) -> RuntimeValue {
                for (const auto& coordinate : point.coordinates) {
                    if (coordinate.type == domain::CoordinateType::Temperature) {
                        return std::get<domain::Temperature>(coordinate.value);
                    }
                }
                throw std::logic_error("operating point has no temperature coordinate");
            },
            [&](const ir::MeasurePower&) -> RuntimeValue {
                return devices.power->measurePower();
            },
            [&](const ir::MeasureEvm&) -> RuntimeValue {
                return devices.evm->measureEvm();
            },
            [&](const ir::MeasureFrequencyError&) -> RuntimeValue {
                return devices.frequency_error->measureFrequencyError();
            },
            [&](const ir::NegateDelta& operation) -> RuntimeValue {
                return std::visit(
                    Overloaded{
                        [](domain::FrequencyDelta value) -> RuntimeValue { return -value; },
                        [](domain::Ratio value) -> RuntimeValue { return -value; },
                        [](domain::TemperatureDelta value) -> RuntimeValue { return -value; },
                        [](const auto&) -> RuntimeValue {
                            throw std::logic_error("invalid delta negation operand");
                        }},
                    values[operation.operand.value]);
            },
            [&](const ir::AddDelta& operation) -> RuntimeValue {
                if (const auto* left = std::get_if<domain::FrequencyDelta>(
                        &values[operation.left.value])) {
                    return *left + slot<domain::FrequencyDelta>(values, operation.right);
                }
                if (const auto* left = std::get_if<domain::Ratio>(&values[operation.left.value])) {
                    return *left + slot<domain::Ratio>(values, operation.right);
                }
                return slot<domain::TemperatureDelta>(values, operation.left) +
                       slot<domain::TemperatureDelta>(values, operation.right);
            },
            [&](const ir::SubtractDelta& operation) -> RuntimeValue {
                if (const auto* left = std::get_if<domain::FrequencyDelta>(
                        &values[operation.left.value])) {
                    return *left - slot<domain::FrequencyDelta>(values, operation.right);
                }
                if (const auto* left = std::get_if<domain::Ratio>(&values[operation.left.value])) {
                    return *left - slot<domain::Ratio>(values, operation.right);
                }
                return slot<domain::TemperatureDelta>(values, operation.left) -
                       slot<domain::TemperatureDelta>(values, operation.right);
            },
            [&](const ir::Alias& operation) -> RuntimeValue {
                return values[operation.value.value];
            },
            [&](const ir::NegatePowerDelta& operation) -> RuntimeValue {
                return -slot<domain::PowerDelta>(values, operation.operand);
            },
            [&](const ir::AddPowerDelta& operation) -> RuntimeValue {
                return slot<domain::PowerDelta>(values, operation.left) +
                       slot<domain::PowerDelta>(values, operation.right);
            },
            [&](const ir::ApplyPowerDelta& operation) -> RuntimeValue {
                return slot<domain::Power>(values, operation.power) +
                       slot<domain::PowerDelta>(values, operation.delta);
            },
            [&](const ir::PowerDifference& operation) -> RuntimeValue {
                return slot<domain::Power>(values, operation.left) -
                       slot<domain::Power>(values, operation.right);
            },
            [&](const ir::AbsoluteValue& operation) -> RuntimeValue {
                return std::visit(
                    Overloaded{
                        [](domain::FrequencyDelta value) -> RuntimeValue {
                            return domain::FrequencyDelta::from_hertz(std::abs(value.hertz()));
                        },
                        [](domain::Ratio value) -> RuntimeValue {
                            return domain::Ratio::from_ratio(std::abs(value.ratio()));
                        },
                        [](domain::TemperatureDelta value) -> RuntimeValue {
                            return domain::TemperatureDelta::from_kelvin(std::abs(value.kelvin()));
                        },
                        [](domain::PowerDelta value) -> RuntimeValue {
                            return domain::PowerDelta::from_db(std::abs(value.db()));
                        },
                        [](const auto&) -> RuntimeValue {
                            throw std::logic_error("invalid abs() operand");
                        }}, values[operation.operand.value]);
            },
            [&](const ir::Compare& operation) -> RuntimeValue {
                const auto scalar = [](const RuntimeValue& value) {
                    return std::visit(Overloaded{
                        [](domain::Frequency v) { return v.hertz(); },
                        [](domain::FrequencyDelta v) { return v.hertz(); },
                        [](domain::Ratio v) { return v.ratio(); },
                        [](domain::Temperature v) { return v.kelvin(); },
                        [](domain::TemperatureDelta v) { return v.kelvin(); },
                        [](domain::Power v) { return v.dbm(); },
                        [](domain::PowerDelta v) { return v.db(); },
                        [](bool) -> double { throw std::logic_error("invalid comparison operand"); }
                    }, value);
                };
                const auto left = scalar(values[operation.left.value]);
                const auto right = scalar(values[operation.right.value]);
                switch (operation.op) {
                case ir::ComparisonOperator::Less: return left < right;
                case ir::ComparisonOperator::LessEqual: return left <= right;
                case ir::ComparisonOperator::Greater: return left > right;
                case ir::ComparisonOperator::GreaterEqual: return left >= right;
                case ir::ComparisonOperator::Equal: return left == right;
                case ir::ComparisonOperator::NotEqual: return left != right;
                }
                throw std::logic_error("invalid comparison operator");
            }},
        definition);
}

bool finite_value(const RuntimeValue& value) {
    return std::visit(Overloaded{
        [](domain::Frequency v) { return std::isfinite(v.hertz()); },
        [](domain::FrequencyDelta v) { return std::isfinite(v.hertz()); },
        [](domain::Ratio v) { return std::isfinite(v.ratio()); },
        [](domain::Temperature v) { return std::isfinite(v.kelvin()); },
        [](domain::TemperatureDelta v) { return std::isfinite(v.kelvin()); },
        [](domain::Power v) { return std::isfinite(v.dbm()); },
        [](domain::PowerDelta v) { return std::isfinite(v.db()); },
        [](bool) { return true; }}, value);
}

} // namespace

std::vector<Diagnostic> detail::validate_program(const ir::Program& program) {
    return Validator{program}.run();
}

PointEvaluation detail::evaluate_validated_point(const ir::Program& program,
                                                 domain::OperatingPoint point,
                                                 DeviceCapabilities devices) {
    domain::Frequency frequency = domain::Frequency::from_hertz(0.0);
    for (const auto& coordinate : point.coordinates) {
        if (coordinate.type == domain::CoordinateType::Frequency) {
            frequency = std::get<domain::Frequency>(coordinate.value);
            devices.rf.setFrequency(frequency);
        } else {
            devices.temperature->setTemperature(
                std::get<domain::Temperature>(coordinate.value));
        }
    }

    PointEvaluation evaluation{frequency, {}, std::move(point)};
    evaluation.values.reserve(program.values.size());
    for (const auto& value : program.values) {
        if (!program.measurements.empty() && value.id == program.measurements.front().value) {
            const auto reference = slot<domain::Power>(evaluation.values, program.reference);
            devices.rf.setOutputPower(reference);
        }
        evaluation.values.push_back(
            evaluate_definition(value.definition, evaluation.operating_point,
                                devices,
                                evaluation.values));
    }
    return evaluation;
}

const RuntimeValue& PointEvaluation::at(ir::ValueId id) const {
    return values.at(id.value);
}

ExecutionResult execute_point(const ir::Program& program, domain::Frequency frequency,
                              device::RfDevice& tester,
                              device::MeasurementDevice& measurement) {
    domain::OperatingPoint point{{domain::Coordinate{
        "frequency", domain::CoordinateType::Frequency, frequency}}};
    return execute_point(program, std::move(point),
                         DeviceCapabilities{tester, &measurement, nullptr});
}

ExecutionResult execute_point(const ir::Program& program,
                              domain::OperatingPoint point,
                              DeviceCapabilities devices) {
    auto diagnostics = detail::validate_program(program);
    if (!diagnostics.empty()) return ExecutionResult{std::nullopt, std::move(diagnostics)};

    for (const auto& measurement : program.measurements) {
        const bool missing =
            (measurement.operation == ir::MeasurementOperation::Power && devices.power == nullptr) ||
            (measurement.operation == ir::MeasurementOperation::Evm && devices.evm == nullptr) ||
            (measurement.operation == ir::MeasurementOperation::FrequencyError &&
             devices.frequency_error == nullptr);
        if (missing) {
            const char* capability = measurement.operation == ir::MeasurementOperation::Power
                                         ? "power"
                                     : measurement.operation == ir::MeasurementOperation::Evm
                                         ? "EVM"
                                         : "frequency-error";
            return ExecutionResult{
                std::nullopt,
                {Diagnostic{DiagnosticCode::CapabilityUnavailable,
                            std::string{capability} + " measurement capability is required",
                            measurement.span}}};
        }
    }
    if (point.coordinates.size() != program.sweeps.size()) {
        return ExecutionResult{
            std::nullopt,
            {Diagnostic{DiagnosticCode::InvalidProgramBinding,
                        "operating point does not match the program sweep shape",
                        program.span}}};
    }
    for (std::size_t index = 0; index < program.sweeps.size(); ++index) {
        const auto& sweep = program.sweeps[index];
        const auto& coordinate = point.coordinates[index];
        const auto expected = sweep.dimension == ir::SweepDimension::Frequency
                                  ? domain::CoordinateType::Frequency
                                  : domain::CoordinateType::Temperature;
        if (coordinate.name != sweep.name || coordinate.type != expected ||
            (expected == domain::CoordinateType::Frequency &&
             !std::holds_alternative<domain::Frequency>(coordinate.value)) ||
            (expected == domain::CoordinateType::Temperature &&
             !std::holds_alternative<domain::Temperature>(coordinate.value))) {
            return ExecutionResult{
                std::nullopt,
                {Diagnostic{DiagnosticCode::InvalidProgramBinding,
                            "operating-point coordinates are not in sweep declaration order",
                            sweep.span}}};
        }
        if (expected == domain::CoordinateType::Temperature &&
            devices.temperature == nullptr) {
            return ExecutionResult{
                std::nullopt,
                {Diagnostic{DiagnosticCode::CapabilityUnavailable,
                            "temperature control capability is required", sweep.span}}};
        }
    }

    auto evaluation = detail::evaluate_validated_point(program, std::move(point), devices);
    for (std::size_t index = 0; index < evaluation.values.size(); ++index) {
        if (!finite_value(evaluation.values[index])) {
            return ExecutionResult{
                std::nullopt,
                {Diagnostic{DiagnosticCode::NonFiniteValue,
                            "runtime value is not finite", program.values[index].span}}};
        }
    }
    return ExecutionResult{std::move(evaluation), {}};
}

} // namespace horusrf::runtime
