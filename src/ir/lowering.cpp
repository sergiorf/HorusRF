#include "horusrf/ir/lowering.hpp"

#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "horusrf/semantic/model.hpp"

namespace horusrf::ir {

std::string_view diagnostic_code_name(DiagnosticCode code) noexcept {
    switch (code) {
    case DiagnosticCode::DuplicateSymbol: return "lowering.duplicate_symbol";
    case DiagnosticCode::UnknownSymbol: return "lowering.unknown_symbol";
    case DiagnosticCode::TypeMismatch: return "lowering.type_mismatch";
    case DiagnosticCode::InvalidDefinitionOrder: return "lowering.invalid_definition_order";
    case DiagnosticCode::InvalidCalibrationIndex: return "lowering.invalid_calibration_index";
    case DiagnosticCode::UnsupportedExpression: return "lowering.unsupported_expression";
    }
    return "lowering.unknown";
}

namespace {

struct Binding {
    ValueId value;
    ValueType type;
};

class Lowerer {
public:
    explicit Lowerer(const semantic::AnalyzedProgram& input)
        : input_(input),
          output_{input.characterization_name,
                  input.span,
                  ValueId{},
                  Sweep{ValueId{}, input.sweep.start, input.sweep.end, input.sweep.step,
                        input.sweep.span},
                  {},
                  input.total_point_count,
                  ValueId{},
                  {},
                  {},
                  {},
                  {},
                  {}} {}

    LoweringResult run() {
        output_.characterization_name = input_.characterization_name;
        output_.span = input_.span;

        const auto reference = emit(ValueType::Power, ReferencePower{input_.reference.value},
                                    input_.reference.span);
        if (!bind(input_.reference.symbol, {reference, ValueType::Power},
                  input_.reference.span)) return failure();
        output_.reference = reference;

        for (const auto& input_sweep : input_.sweeps) {
            const bool lowered = std::visit(
                [this](const auto& sweep) {
                    using T = std::decay_t<decltype(sweep)>;
                    if constexpr (std::is_same_v<T, semantic::FrequencySweep>) {
                        const auto value = emit(ValueType::Frequency, SweepFrequency{}, sweep.span);
                        if (!bind(sweep.symbol, {value, ValueType::Frequency}, sweep.span)) {
                            return false;
                        }
                        output_.sweeps.push_back(TypedSweep{
                            value, "frequency", SweepDimension::Frequency, sweep.start,
                            sweep.end, sweep.step, sweep.point_count, sweep.span});
                        output_.sweep = Sweep{value, sweep.start, sweep.end, sweep.step, sweep.span};
                    } else {
                        const auto value =
                            emit(ValueType::Temperature, SweepTemperature{}, sweep.span);
                        if (!bind(sweep.symbol, {value, ValueType::Temperature}, sweep.span)) {
                            return false;
                        }
                        output_.sweeps.push_back(TypedSweep{
                            value, "temperature", SweepDimension::Temperature, sweep.start,
                            sweep.end, sweep.step, sweep.point_count, sweep.span});
                    }
                    return true;
                },
                input_sweep);
            if (!lowered) return failure();
        }

        for (const auto& source : input_.measurements) {
            const auto type = convert_type(source.type);
            if (!type) return failure();
            Definition definition = MeasurePower{};
            MeasurementOperation operation = MeasurementOperation::Power;
            if (source.operation == semantic::MeasurementOperation::Evm) {
                definition = MeasureEvm{};
                operation = MeasurementOperation::Evm;
            } else if (source.operation == semantic::MeasurementOperation::FrequencyError) {
                definition = MeasureFrequencyError{};
                operation = MeasurementOperation::FrequencyError;
            }
            const auto value = emit(*type, std::move(definition), source.span);
            if (!bind(source.symbol, {value, *type}, source.span)) return failure();
            output_.measurements.push_back(
                Measurement{source.name, operation, value, *type, source.legacy, source.span});
            if (source.symbol == input_.measurement.symbol) output_.measurement = value;
        }

        for (const auto& derived : input_.derived_quantities) {
            if (!reserve_symbol(derived.symbol, derived.span)) return failure();
            const auto declared_type = convert_type(derived.type);
            if (!declared_type) {
                report(DiagnosticCode::TypeMismatch, "derived declaration has an invalid type",
                       derived.span);
                return failure();
            }
            auto root = lower_expression(derived.expression);
            if (!root) return failure();
            if (root->type != *declared_type) {
                report(DiagnosticCode::TypeMismatch,
                       "derived declaration type does not match its expression", derived.span);
                return failure();
            }
            if (is_leaf(derived.expression)) {
                root = TypedValue{emit(root->type, Alias{root->id}, derived.expression.span),
                                  root->type};
            }
            symbols_.emplace(derived.symbol.value, Binding{root->id, root->type});
            output_.named_values.push_back(
                NamedValue{derived.name, root->id, root->type, derived.span});
        }

        for (const auto& assertion : input_.assertions) {
            auto root = lower_expression(assertion.expression);
            if (!root || root->type != ValueType::Boolean) {
                report(DiagnosticCode::TypeMismatch,
                       "assertion expression must lower to Boolean", assertion.span);
                return failure();
            }
            const auto* definition = comparison_definition(root->id);
            if (!definition) {
                report(DiagnosticCode::UnsupportedExpression,
                       "assertion must be backed by a comparison", assertion.span);
                return failure();
            }
            output_.assertions.push_back(Assertion{
                assertion.id, assertion.ordinal, root->id, definition->left,
                definition->right, definition->operand_type, definition->op,
                assertion.span});
        }

        const auto& calibration = input_.calibration;
        if (!reserve_symbol(calibration.symbol, calibration.span)) return failure();
        const auto correction_type = convert_type(calibration.type.correction_type);
        if (!correction_type || *correction_type != ValueType::PowerDelta ||
            calibration.correction.type != semantic::SemanticType::PowerDelta) {
            report(DiagnosticCode::TypeMismatch,
                   "calibration correction must have type PowerDelta", calibration.span);
            return failure();
        }
        auto correction = lower_expression(calibration.correction);
        if (!correction) return failure();
        if (correction->type != ValueType::PowerDelta) {
            report(DiagnosticCode::TypeMismatch,
                   "calibration correction value is not PowerDelta",
                   calibration.correction.span);
            return failure();
        }
        if (calibration.indexes.size() != calibration.type.index_types.size()) {
            report(DiagnosticCode::InvalidCalibrationIndex,
                   "calibration index metadata has inconsistent lengths", calibration.span);
            return failure();
        }
        if (calibration.indexes.empty()) {
            report(DiagnosticCode::InvalidCalibrationIndex,
                   "calibration must have an active sweep index", calibration.span);
            return failure();
        }
        Calibration lowered{calibration.name, correction->id, {}, {}, ValueType::PowerDelta,
                            calibration.span};
        std::unordered_set<std::size_t> indexes;
        for (std::size_t i = 0; i < calibration.indexes.size(); ++i) {
            const auto symbol = calibration.indexes[i];
            const auto found = symbols_.find(symbol.value);
            const auto semantic_type = convert_type(calibration.type.index_types[i]);
            if (found == symbols_.end()) {
                report(DiagnosticCode::InvalidCalibrationIndex,
                       "calibration index refers to an unknown symbol", calibration.span);
                return failure();
            }
            if (!indexes.insert(symbol.value).second) {
                report(DiagnosticCode::InvalidCalibrationIndex,
                       "calibration contains a duplicate index", calibration.span);
                return failure();
            }
            if (!semantic_type || i >= output_.sweeps.size() ||
                found->second.type != *semantic_type ||
                found->second.value != output_.sweeps[i].value ||
                (*semantic_type != ValueType::Frequency &&
                 *semantic_type != ValueType::Temperature)) {
                report(DiagnosticCode::InvalidCalibrationIndex,
                       "calibration index is not the active frequency sweep",
                       calibration.span);
                return failure();
            }
            lowered.indexes.push_back(found->second.value);
            lowered.index_types.push_back(*semantic_type);
        }
        output_.calibration = std::move(lowered);

        return LoweringResult{std::move(output_), {}};
    }

private:
    struct TypedValue {
        ValueId id;
        ValueType type;
    };

    LoweringResult failure() { return LoweringResult{std::nullopt, std::move(diagnostics_)}; }

    void report(DiagnosticCode code, std::string message, parser::SourceSpan span) {
        diagnostics_.push_back(Diagnostic{code, std::move(message), span});
    }

    static std::optional<ValueType> convert_type(semantic::SemanticType type) {
        switch (type) {
        case semantic::SemanticType::Frequency: return ValueType::Frequency;
        case semantic::SemanticType::FrequencyDelta: return ValueType::FrequencyDelta;
        case semantic::SemanticType::Ratio: return ValueType::Ratio;
        case semantic::SemanticType::Temperature: return ValueType::Temperature;
        case semantic::SemanticType::TemperatureDelta: return ValueType::TemperatureDelta;
        case semantic::SemanticType::Power: return ValueType::Power;
        case semantic::SemanticType::PowerDelta: return ValueType::PowerDelta;
        case semantic::SemanticType::Boolean: return ValueType::Boolean;
        }
        return std::nullopt;
    }

    ValueId emit(ValueType type, Definition definition, parser::SourceSpan span) {
        const ValueId id{output_.values.size()};
        output_.values.push_back(Value{id, type, std::move(definition), span});
        return id;
    }

    bool reserve_symbol(semantic::SymbolId symbol, parser::SourceSpan span) {
        if (!used_symbols_.insert(symbol.value).second) {
            report(DiagnosticCode::DuplicateSymbol, "duplicate semantic symbol", span);
            return false;
        }
        return true;
    }

    bool bind(semantic::SymbolId symbol, Binding binding, parser::SourceSpan span) {
        if (!reserve_symbol(symbol, span)) return false;
        symbols_.emplace(symbol.value, binding);
        return true;
    }

    static bool is_leaf(const semantic::Expression& expression) {
        return std::holds_alternative<semantic::SymbolValue>(expression.value) ||
               std::holds_alternative<semantic::ReferenceValue>(expression.value);
    }

    const Compare* comparison_definition(ValueId value) const {
        while (value.value < output_.values.size()) {
            const auto& definition = output_.values[value.value].definition;
            if (const auto* comparison = std::get_if<Compare>(&definition)) {
                return comparison;
            }
            const auto* alias = std::get_if<Alias>(&definition);
            if (!alias || alias->value.value >= value.value) return nullptr;
            value = alias->value;
        }
        return nullptr;
    }

    std::optional<TypedValue> lower_expression(const semantic::Expression& expression) {
        return std::visit(
            [this, &expression](const auto& value) {
                return lower_value(value, expression.type, expression.span);
            },
            expression.value);
    }

    std::optional<TypedValue> lower_value(const semantic::QuantityValue& quantity,
                                          semantic::SemanticType outer_type,
                                          parser::SourceSpan outer_span) {
        ValueType actual = ValueType::PowerDelta;
        if (std::holds_alternative<domain::Frequency>(quantity.value)) {
            actual = ValueType::Frequency;
        } else if (std::holds_alternative<domain::FrequencyDelta>(quantity.value)) {
            actual = ValueType::FrequencyDelta;
        } else if (std::holds_alternative<domain::Ratio>(quantity.value)) {
            actual = ValueType::Ratio;
        } else if (std::holds_alternative<domain::Temperature>(quantity.value)) {
            actual = ValueType::Temperature;
        } else if (std::holds_alternative<domain::TemperatureDelta>(quantity.value)) {
            actual = ValueType::TemperatureDelta;
        } else if (std::holds_alternative<domain::Power>(quantity.value)) {
            actual = ValueType::Power;
        }
        const auto outer = convert_type(outer_type);
        if (!outer || *outer != actual) {
            report(DiagnosticCode::TypeMismatch,
                   "quantity expression type does not match its value", outer_span);
            return std::nullopt;
        }
        const auto constant = std::visit(
            [](const auto& value) { return ConstantValue{value}; }, quantity.value);
        return TypedValue{emit(actual, Constant{constant}, quantity.span), actual};
    }

    template <typename Leaf>
    std::optional<TypedValue> lower_leaf(const Leaf& leaf, semantic::SemanticType outer_type,
                                         parser::SourceSpan outer_span) {
        const auto found = symbols_.find(leaf.symbol.value);
        if (found == symbols_.end()) {
            report(DiagnosticCode::UnknownSymbol, "expression refers to an unmapped symbol",
                   leaf.span);
            return std::nullopt;
        }
        const auto leaf_type = convert_type(leaf.type);
        const auto outer = convert_type(outer_type);
        if (!leaf_type || !outer || *leaf_type != found->second.type ||
            *outer != found->second.type) {
            report(DiagnosticCode::TypeMismatch,
                   "symbol expression type does not match the mapped value", outer_span);
            return std::nullopt;
        }
        if (found->second.value.value >= output_.values.size()) {
            report(DiagnosticCode::InvalidDefinitionOrder,
                   "symbol refers to a value that is not defined yet", leaf.span);
            return std::nullopt;
        }
        return TypedValue{found->second.value, found->second.type};
    }

    std::optional<TypedValue> lower_value(const semantic::SymbolValue& symbol,
                                          semantic::SemanticType outer_type,
                                          parser::SourceSpan outer_span) {
        return lower_leaf(symbol, outer_type, outer_span);
    }

    std::optional<TypedValue> lower_value(const semantic::ReferenceValue& reference,
                                          semantic::SemanticType outer_type,
                                          parser::SourceSpan outer_span) {
        return lower_leaf(reference, outer_type, outer_span);
    }

    std::optional<TypedValue> lower_value(const semantic::UnaryValue& unary,
                                          semantic::SemanticType outer_type,
                                          parser::SourceSpan outer_span) {
        if (!unary.operand || unary.op != semantic::UnaryOperator::Negate) {
            report(DiagnosticCode::UnsupportedExpression,
                   "unsupported or incomplete unary expression", outer_span);
            return std::nullopt;
        }
        auto operand = lower_expression(*unary.operand);
        const auto node_type = convert_type(unary.type);
        const auto outer = convert_type(outer_type);
        if (!operand) return std::nullopt;
        if (!node_type || !outer || operand->type != *node_type || *node_type != *outer ||
            (*node_type != ValueType::PowerDelta &&
             *node_type != ValueType::FrequencyDelta &&
             *node_type != ValueType::Ratio &&
             *node_type != ValueType::TemperatureDelta)) {
            report(DiagnosticCode::TypeMismatch,
                   "negation requires and preserves a delta or ratio type", outer_span);
            return std::nullopt;
        }
        if (*node_type == ValueType::PowerDelta) {
            return TypedValue{emit(*node_type, NegatePowerDelta{operand->id}, unary.span),
                              *node_type};
        }
        return TypedValue{emit(*node_type, NegateDelta{operand->id}, unary.span), *node_type};
    }

    std::optional<TypedValue> lower_value(const semantic::BinaryValue& binary,
                                          semantic::SemanticType outer_type,
                                          parser::SourceSpan outer_span) {
        if (!binary.left || !binary.right) {
            report(DiagnosticCode::UnsupportedExpression, "incomplete binary expression",
                   outer_span);
            return std::nullopt;
        }
        auto left = lower_expression(*binary.left);
        if (!left) return std::nullopt;
        auto right = lower_expression(*binary.right);
        if (!right) return std::nullopt;

        std::optional<ValueType> result_type;
        std::optional<Definition> definition;
        if (binary.op == semantic::BinaryOperator::Add && left->type == ValueType::Power &&
            right->type == ValueType::PowerDelta) {
            result_type = ValueType::Power;
            definition = ApplyPowerDelta{left->id, right->id};
        } else if (binary.op == semantic::BinaryOperator::Add &&
                   left->type == ValueType::PowerDelta &&
                   right->type == ValueType::PowerDelta) {
            result_type = ValueType::PowerDelta;
            definition = AddPowerDelta{left->id, right->id};
        } else if (binary.op == semantic::BinaryOperator::Subtract &&
                   left->type == ValueType::Power && right->type == ValueType::Power) {
            result_type = ValueType::PowerDelta;
            definition = PowerDifference{left->id, right->id};
        } else if (left->type == right->type &&
                   (left->type == ValueType::FrequencyDelta ||
                    left->type == ValueType::Ratio ||
                    left->type == ValueType::TemperatureDelta)) {
            result_type = left->type;
            definition = binary.op == semantic::BinaryOperator::Add
                             ? Definition{AddDelta{left->id, right->id}}
                             : Definition{SubtractDelta{left->id, right->id}};
        } else {
            report(DiagnosticCode::UnsupportedExpression,
                   "unsupported binary operator and operand types", binary.span);
            return std::nullopt;
        }

        const auto node_type = convert_type(binary.type);
        const auto outer = convert_type(outer_type);
        if (!node_type || !outer || *node_type != *result_type || *outer != *result_type) {
            report(DiagnosticCode::TypeMismatch,
                   "binary expression stored type does not match its definition", outer_span);
            return std::nullopt;
        }
        return TypedValue{emit(*result_type, std::move(*definition), binary.span), *result_type};
    }

    std::optional<TypedValue> lower_value(const semantic::CallValue& call,
                                          semantic::SemanticType outer_type,
                                          parser::SourceSpan outer_span) {
        if (call.function != "abs" || !call.argument) {
            report(DiagnosticCode::UnsupportedExpression, "unsupported function call",
                   outer_span);
            return std::nullopt;
        }
        auto argument = lower_expression(*call.argument);
        const auto expected = convert_type(call.type);
        const auto outer = convert_type(outer_type);
        if (!argument || !expected || !outer || argument->type != *expected ||
            *expected != *outer || *expected == ValueType::Boolean ||
            *expected == ValueType::Frequency || *expected == ValueType::Temperature ||
            *expected == ValueType::Power) {
            report(DiagnosticCode::TypeMismatch, "invalid abs() argument type", outer_span);
            return std::nullopt;
        }
        return TypedValue{emit(*expected, AbsoluteValue{argument->id}, call.span), *expected};
    }

    std::optional<TypedValue> lower_value(const semantic::ComparisonValue& comparison,
                                          semantic::SemanticType outer_type,
                                          parser::SourceSpan outer_span) {
        if (!comparison.left || !comparison.right) return std::nullopt;
        auto left = lower_expression(*comparison.left);
        auto right = lower_expression(*comparison.right);
        const auto outer = convert_type(outer_type);
        if (!left || !right || !outer || *outer != ValueType::Boolean ||
            left->type != right->type || left->type == ValueType::Boolean) {
            report(DiagnosticCode::TypeMismatch, "invalid comparison operand types", outer_span);
            return std::nullopt;
        }
        ComparisonOperator op = ComparisonOperator::Equal;
        switch (comparison.op) {
        case semantic::ComparisonOperator::Less: op = ComparisonOperator::Less; break;
        case semantic::ComparisonOperator::LessEqual: op = ComparisonOperator::LessEqual; break;
        case semantic::ComparisonOperator::Greater: op = ComparisonOperator::Greater; break;
        case semantic::ComparisonOperator::GreaterEqual: op = ComparisonOperator::GreaterEqual; break;
        case semantic::ComparisonOperator::NotEqual: op = ComparisonOperator::NotEqual; break;
        default: break;
        }
        return TypedValue{emit(ValueType::Boolean,
                               Compare{op, left->id, right->id, left->type},
                               comparison.span), ValueType::Boolean};
    }

    const semantic::AnalyzedProgram& input_;
    Program output_;
    std::vector<Diagnostic> diagnostics_;
    std::unordered_map<std::size_t, Binding> symbols_;
    std::unordered_set<std::size_t> used_symbols_;
};

} // namespace

LoweringResult lower(const semantic::AnalyzedProgram& program) { return Lowerer(program).run(); }

} // namespace horusrf::ir
