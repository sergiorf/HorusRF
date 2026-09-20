#include "horusrf/runtime/executor.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace horusrf::runtime {
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
        validate_calibrations();
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
                            [](domain::Power) { return ir::ValueType::Power; },
                            [](domain::PowerDelta) { return ir::ValueType::PowerDelta; }},
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
                [&](const ir::MeasurePower&) {
                    return value.type == ir::ValueType::Power ||
                           report(DiagnosticCode::TypeMismatch,
                                  "measurement definition must have type Power", value.span);
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
            sweeps += std::holds_alternative<ir::SweepFrequency>(value.definition) ? 1U : 0U;
            measurements += std::holds_alternative<ir::MeasurePower>(value.definition) ? 1U : 0U;
        }
        if (references != 1 || sweeps != 1 || measurements != 1) {
            return report(DiagnosticCode::InvalidProgramBinding,
                          "program must contain exactly one reference, sweep, and measurement",
                          program_.span);
        }
        return binding(program_.reference, ir::ValueType::Power,
                       ir::Definition{ir::ReferencePower{
                           domain::Power::from_dbm(0.0)}}.index(),
                       program_.span, "reference") &&
               binding(program_.sweep.frequency, ir::ValueType::Frequency,
                       ir::Definition{ir::SweepFrequency{}}.index(), program_.sweep.span,
                       "sweep") &&
               binding(program_.measurement, ir::ValueType::Power,
                       ir::Definition{ir::MeasurePower{}}.index(), program_.span,
                       "measurement");
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

    bool validate_calibrations() {
        for (const auto& calibration : program_.calibrations) {
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
            if (calibration.indexes.size() != 1 ||
                calibration.index_types.size() != 1 ||
                calibration.indexes.front() != program_.sweep.frequency ||
                calibration.index_types.front() != ir::ValueType::Frequency) {
                return report(DiagnosticCode::InvalidProgramBinding,
                              "calibration must be indexed by the active frequency sweep",
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
                                 const ir::Program& program,
                                 domain::Frequency frequency,
                                 device::RfDevice& device,
                                 const std::vector<RuntimeValue>& values) {
    return std::visit(
        Overloaded{
            [](const ir::Constant& operation) -> RuntimeValue {
                return std::visit([](const auto& value) -> RuntimeValue { return value; },
                                  operation.value);
            },
            [](const ir::ReferencePower& operation) -> RuntimeValue { return operation.value; },
            [frequency](const ir::SweepFrequency&) -> RuntimeValue { return frequency; },
            [&](const ir::MeasurePower&) -> RuntimeValue {
                const auto point = slot<domain::Frequency>(values, program.sweep.frequency);
                const auto reference = slot<domain::Power>(values, program.reference);
                device.setFrequency(point);
                device.setOutputPower(reference);
                return device.measurePower();
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
            }},
        definition);
}

} // namespace

const RuntimeValue& PointEvaluation::at(ir::ValueId id) const {
    return values.at(id.value);
}

ExecutionResult execute_point(const ir::Program& program, domain::Frequency frequency,
                              device::RfDevice& device) {
    auto diagnostics = Validator{program}.run();
    if (!diagnostics.empty()) return ExecutionResult{std::nullopt, std::move(diagnostics)};

    PointEvaluation evaluation{frequency, {}};
    evaluation.values.reserve(program.values.size());
    for (const auto& value : program.values) {
        evaluation.values.push_back(
            evaluate_definition(value.definition, program, frequency, device,
                                evaluation.values));
    }
    return ExecutionResult{std::move(evaluation), {}};
}

} // namespace horusrf::runtime
