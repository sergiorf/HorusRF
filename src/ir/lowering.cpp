#include "horusrf/ir/lowering.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "horusrf/semantic/model.hpp"

namespace horusrf::ir {
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
                  ValueId{},
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

        const auto frequency = emit(ValueType::Frequency, SweepFrequency{}, input_.sweep.span);
        if (!bind(input_.sweep.symbol, {frequency, ValueType::Frequency}, input_.sweep.span)) {
            return failure();
        }
        output_.sweep = Sweep{frequency, input_.sweep.start, input_.sweep.end,
                              input_.sweep.step, input_.sweep.span};

        const auto measurement = emit(ValueType::Power, MeasurePower{}, input_.measurement.span);
        if (!bind(input_.measurement.symbol, {measurement, ValueType::Power},
                  input_.measurement.span)) return failure();
        output_.measurement = measurement;

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

        for (const auto& calibration : input_.calibrations) {
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
                if (!semantic_type || *semantic_type != ValueType::Frequency ||
                    found->second.type != ValueType::Frequency ||
                    found->second.value != output_.sweep.frequency) {
                    report(DiagnosticCode::InvalidCalibrationIndex,
                           "calibration index is not the active frequency sweep",
                           calibration.span);
                    return failure();
                }
                lowered.indexes.push_back(found->second.value);
                lowered.index_types.push_back(ValueType::Frequency);
            }
            output_.calibrations.push_back(std::move(lowered));
        }

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
        case semantic::SemanticType::Power: return ValueType::Power;
        case semantic::SemanticType::PowerDelta: return ValueType::PowerDelta;
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
        } else if (std::holds_alternative<domain::Power>(quantity.value)) {
            actual = ValueType::Power;
        }
        const auto outer = convert_type(outer_type);
        if (!outer || *outer != actual) {
            report(DiagnosticCode::TypeMismatch,
                   "quantity expression type does not match its value", outer_span);
            return std::nullopt;
        }
        return TypedValue{emit(actual, Constant{quantity.value}, quantity.span), actual};
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
        if (!node_type || !outer || operand->type != ValueType::PowerDelta ||
            *node_type != ValueType::PowerDelta || *outer != ValueType::PowerDelta) {
            report(DiagnosticCode::TypeMismatch,
                   "negation requires and produces PowerDelta", outer_span);
            return std::nullopt;
        }
        return TypedValue{emit(ValueType::PowerDelta, NegatePowerDelta{operand->id}, unary.span),
                          ValueType::PowerDelta};
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

    const semantic::AnalyzedProgram& input_;
    Program output_;
    std::vector<Diagnostic> diagnostics_;
    std::unordered_map<std::size_t, Binding> symbols_;
    std::unordered_set<std::size_t> used_symbols_;
};

} // namespace

LoweringResult lower(const semantic::AnalyzedProgram& program) { return Lowerer(program).run(); }

} // namespace horusrf::ir
