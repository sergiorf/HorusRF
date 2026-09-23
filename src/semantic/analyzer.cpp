#include "horusrf/semantic/analyzer.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace horusrf::semantic {

std::string_view diagnostic_code_name(DiagnosticCode code) noexcept {
    switch (code) {
    case DiagnosticCode::InvalidNumericLiteral: return "semantic.invalid_numeric_literal";
    case DiagnosticCode::UnexpectedQuantityType: return "semantic.unexpected_quantity_type";
    case DiagnosticCode::DuplicateDeclaration: return "semantic.duplicate_declaration";
    case DiagnosticCode::MissingReference: return "semantic.missing_reference";
    case DiagnosticCode::MissingSweep: return "semantic.missing_sweep";
    case DiagnosticCode::MissingMeasurement: return "semantic.missing_measurement";
    case DiagnosticCode::UnknownIdentifier: return "semantic.unknown_identifier";
    case DiagnosticCode::UnknownReferenceMember: return "semantic.unknown_reference_member";
    case DiagnosticCode::ReferenceNotAvailable: return "semantic.reference_not_available";
    case DiagnosticCode::InvalidUnaryOperand: return "semantic.invalid_unary_operand";
    case DiagnosticCode::InvalidBinaryOperands: return "semantic.invalid_binary_operands";
    case DiagnosticCode::InvalidSweepRange: return "semantic.invalid_sweep_range";
    case DiagnosticCode::InvalidSweepStep: return "semantic.invalid_sweep_step";
    case DiagnosticCode::UnknownCalibrationDimension:
        return "semantic.unknown_calibration_dimension";
    case DiagnosticCode::NonSweepCalibrationDimension:
        return "semantic.non_sweep_calibration_dimension";
    case DiagnosticCode::DuplicateCalibrationDimension:
        return "semantic.duplicate_calibration_dimension";
    case DiagnosticCode::InvalidCalibrationCorrection:
        return "semantic.invalid_calibration_correction";
    }
    return "semantic.unknown";
}

namespace {

struct Symbol {
    SymbolId id;
    SemanticType type;
    bool sweep{};
    bool expression_value{true};
};

SemanticType canonical_type(const domain::CanonicalQuantity& value) {
    if (std::holds_alternative<domain::Frequency>(value)) return SemanticType::Frequency;
    if (std::holds_alternative<domain::Power>(value)) return SemanticType::Power;
    return SemanticType::PowerDelta;
}

class Analyzer {
public:
    explicit Analyzer(const ast::Program& input) : input_(input) {
        for (const auto& statement : input_.characterization.statements) {
            if (const auto* derive = std::get_if<ast::DeriveStatement>(&statement)) {
                future_derived_.insert(derive->name);
            }
        }
    }

    AnalysisResult run() {
        for (const auto& statement : input_.characterization.statements) {
            std::visit([this](const auto& value) { analyze_statement(value); }, statement);
        }
        add_missing_declarations();

        AnalysisResult result;
        result.diagnostics = std::move(diagnostics_);
        if (result.diagnostics.empty()) {
            result.program.emplace(AnalyzedProgram{
                input_.characterization.name, input_.characterization.span,
                std::move(*reference_), std::move(*sweep_), std::move(*measurement_),
                std::move(derived_), std::move(calibrations_)});
        }
        return result;
    }

private:
    void report(DiagnosticCode code, std::string message, parser::SourceSpan span) {
        diagnostics_.push_back(Diagnostic{code, std::move(message), span});
    }

    SymbolId next_symbol() { return SymbolId{next_symbol_++}; }

    std::optional<domain::CanonicalQuantity> quantity(const ast::QuantityLiteral& literal) {
        auto converted = domain::convert_quantity(literal.number, literal.unit);
        if (!converted.value) {
            std::string detail;
            switch (*converted.error) {
            case domain::QuantityConversionError::InvalidNumber: detail = "invalid number"; break;
            case domain::QuantityConversionError::NumberOutOfRange:
                detail = "number is out of range";
                break;
            case domain::QuantityConversionError::UnknownUnit: detail = "unknown unit"; break;
            }
            report(DiagnosticCode::InvalidNumericLiteral,
                   detail + " in quantity '" + literal.number + " " + literal.unit + "'",
                   literal.span);
            return std::nullopt;
        }
        return std::move(*converted.value);
    }

    template <typename T>
    std::optional<T> quantity_as(const ast::QuantityLiteral& literal, SemanticType expected) {
        auto value = quantity(literal);
        if (!value) return std::nullopt;
        if (const auto* typed = std::get_if<T>(&*value)) return *typed;
        report(DiagnosticCode::UnexpectedQuantityType,
               "expected " + std::string(type_name(expected)) + ", got " +
                   type_name(canonical_type(*value)),
               literal.span);
        return std::nullopt;
    }

    void analyze_statement(const ast::ReferenceStatement& statement) {
        if (seen_reference_) {
            report(DiagnosticCode::DuplicateDeclaration, "duplicate reference power declaration",
                   statement.span);
            return;
        }
        seen_reference_ = true;
        auto power = quantity_as<domain::Power>(statement.power, SemanticType::Power);
        if (!power) return;
        const auto id = next_symbol();
        reference_.emplace(ReferencePower{id, *power, statement.span});
        reference_symbol_ = Symbol{id, SemanticType::Power, false, true};
    }

    void analyze_statement(const ast::SweepStatement& statement) {
        if (seen_sweep_) {
            report(DiagnosticCode::DuplicateDeclaration, "duplicate sweep frequency declaration",
                   statement.span);
            return;
        }
        seen_sweep_ = true;
        auto start = quantity_as<domain::Frequency>(statement.start, SemanticType::Frequency);
        auto end = quantity_as<domain::Frequency>(statement.end, SemanticType::Frequency);
        auto step = quantity_as<domain::Frequency>(statement.step, SemanticType::Frequency);
        bool valid = start.has_value() && end.has_value() && step.has_value();
        if (step && step->hertz() <= 0.0) {
            report(DiagnosticCode::InvalidSweepStep, "sweep step must be strictly positive",
                   statement.step.span);
            valid = false;
        }
        if (start && end && end->hertz() < start->hertz()) {
            report(DiagnosticCode::InvalidSweepRange, "sweep end must not precede its start",
                   statement.span);
            valid = false;
        }
        if (!valid) return;
        const auto id = next_symbol();
        sweep_.emplace(FrequencySweep{id, *start, *end, *step, statement.span});
        symbols_.emplace("frequency", Symbol{id, SemanticType::Frequency, true, true});
    }

    void analyze_statement(const ast::MeasurementStatement& statement) {
        if (seen_measurement_) {
            report(DiagnosticCode::DuplicateDeclaration, "duplicate measure power declaration",
                   statement.span);
            return;
        }
        seen_measurement_ = true;
        const auto id = next_symbol();
        measurement_.emplace(PowerMeasurement{id, statement.span});
        symbols_.emplace("power", Symbol{id, SemanticType::Power, false, true});
    }

    bool user_name_available(const std::string& name, parser::SourceSpan span) {
        if (name == "frequency" || name == "power" || declared_user_names_.contains(name)) {
            report(DiagnosticCode::DuplicateDeclaration,
                   "duplicate declaration of '" + name + "'", span);
            return false;
        }
        declared_user_names_.insert(name);
        return true;
    }

    void analyze_statement(const ast::DeriveStatement& statement) {
        future_derived_.erase(statement.name);
        const bool name_valid = user_name_available(statement.name, statement.span);
        auto expression = analyze_expression(statement.expression);
        if (!name_valid || !expression) return;
        const auto id = next_symbol();
        const auto type = expression->type;
        derived_.push_back(DerivedQuantity{id, statement.name, std::move(*expression), type,
                                           statement.span});
        symbols_.emplace(statement.name, Symbol{id, type, false, true});
    }

    void analyze_statement(const ast::CalibrationStatement& statement) {
        const bool name_valid = user_name_available(statement.name, statement.span);
        auto correction = analyze_expression(statement.correction);
        bool valid = name_valid && correction.has_value();
        if (correction && correction->type != SemanticType::PowerDelta) {
            report(DiagnosticCode::InvalidCalibrationCorrection,
                   "calibration correction must be PowerDelta, got " +
                       std::string(type_name(correction->type)),
                   statement.correction.span);
            valid = false;
        }

        std::vector<SymbolId> indexes;
        std::vector<SemanticType> index_types;
        std::unordered_set<std::string> used;
        for (const auto& name : statement.dimensions) {
            if (!used.insert(name).second) {
                report(DiagnosticCode::DuplicateCalibrationDimension,
                       "duplicate calibration dimension '" + name + "'", statement.span);
                valid = false;
                continue;
            }
            const auto found = symbols_.find(name);
            if (found == symbols_.end()) {
                report(DiagnosticCode::UnknownCalibrationDimension,
                       "unknown calibration dimension '" + name + "'", statement.span);
                valid = false;
                continue;
            }
            if (!found->second.sweep) {
                report(DiagnosticCode::NonSweepCalibrationDimension,
                       "calibration dimension '" + name + "' is not a sweep", statement.span);
                valid = false;
                continue;
            }
            indexes.push_back(found->second.id);
            index_types.push_back(found->second.type);
        }

        if (!valid) return;
        const auto id = next_symbol();
        calibrations_.push_back(Calibration{
            id, statement.name, std::move(*correction), std::move(indexes),
            CalibrationType{std::move(index_types), SemanticType::PowerDelta}, statement.span});
        symbols_.emplace(statement.name,
                         Symbol{id, SemanticType::PowerDelta, false, false});
    }

    std::optional<Expression> analyze_expression(const ast::Expression& expression) {
        return std::visit(
            [this, &expression](const auto& value) {
                return analyze_expression_value(value, expression.span);
            },
            expression.value);
    }

    std::optional<Expression> analyze_expression_value(const ast::QuantityLiteral& literal,
                                                       parser::SourceSpan span) {
        auto value = quantity(literal);
        if (!value) return std::nullopt;
        const auto type = canonical_type(*value);
        return Expression{QuantityValue{std::move(*value), literal.span}, type, span};
    }

    std::optional<Expression> analyze_expression_value(const ast::IdentifierExpression& identifier,
                                                       parser::SourceSpan span) {
        const auto found = symbols_.find(identifier.name);
        if (found != symbols_.end() && found->second.expression_value) {
            return Expression{SymbolValue{found->second.id, identifier.name, found->second.type,
                                          identifier.span},
                              found->second.type, span};
        }
        if (identifier.name == "power" || identifier.name == "frequency" ||
            future_derived_.contains(identifier.name)) {
            report(DiagnosticCode::ReferenceNotAvailable,
                   "'" + identifier.name + "' is not available at this point", identifier.span);
        } else {
            report(DiagnosticCode::UnknownIdentifier,
                   "unknown identifier '" + identifier.name + "'", identifier.span);
        }
        return std::nullopt;
    }

    std::optional<Expression> analyze_expression_value(const ast::ReferenceExpression& reference,
                                                       parser::SourceSpan span) {
        if (reference.object != "reference" || reference.member != "power") {
            report(DiagnosticCode::UnknownReferenceMember,
                   "unknown reference member '" + reference.object + "." + reference.member + "'",
                   reference.span);
            return std::nullopt;
        }
        if (!reference_symbol_) {
            report(DiagnosticCode::ReferenceNotAvailable,
                   "reference.power is not available at this point", reference.span);
            return std::nullopt;
        }
        return Expression{ReferenceValue{reference_symbol_->id, reference.object, reference.member,
                                         SemanticType::Power, reference.span},
                          SemanticType::Power, span};
    }

    std::optional<Expression> analyze_expression_value(const ast::UnaryExpression& unary,
                                                       parser::SourceSpan span) {
        if (!unary.operand) return std::nullopt;
        auto operand = analyze_expression(*unary.operand);
        if (!operand) return std::nullopt;
        if (operand->type != SemanticType::PowerDelta) {
            report(DiagnosticCode::InvalidUnaryOperand,
                   "cannot negate " + std::string(type_name(operand->type)), unary.span);
            return std::nullopt;
        }
        auto pointer = std::make_shared<Expression>(std::move(*operand));
        return Expression{UnaryValue{UnaryOperator::Negate, std::move(pointer),
                                     SemanticType::PowerDelta, unary.span},
                          SemanticType::PowerDelta, span};
    }

    std::optional<Expression> analyze_expression_value(const ast::BinaryExpression& binary,
                                                       parser::SourceSpan span) {
        if (!binary.left || !binary.right) return std::nullopt;
        auto left = analyze_expression(*binary.left);
        auto right = analyze_expression(*binary.right);
        if (!left || !right) return std::nullopt;

        std::optional<SemanticType> result;
        if (binary.op == ast::BinaryOperator::Add) {
            if (left->type == SemanticType::Power && right->type == SemanticType::PowerDelta) {
                result = SemanticType::Power;
            } else if (left->type == SemanticType::PowerDelta &&
                       right->type == SemanticType::PowerDelta) {
                result = SemanticType::PowerDelta;
            }
        } else if (left->type == SemanticType::Power && right->type == SemanticType::Power) {
            result = SemanticType::PowerDelta;
        }

        if (!result) {
            report(DiagnosticCode::InvalidBinaryOperands,
                   std::string("cannot ") +
                       (binary.op == ast::BinaryOperator::Add ? "add " : "subtract ") +
                       type_name(left->type) + " and " + type_name(right->type),
                   binary.span);
            return std::nullopt;
        }

        auto left_pointer = std::make_shared<Expression>(std::move(*left));
        auto right_pointer = std::make_shared<Expression>(std::move(*right));
        const auto op = binary.op == ast::BinaryOperator::Add ? BinaryOperator::Add
                                                              : BinaryOperator::Subtract;
        return Expression{BinaryValue{op, std::move(left_pointer), std::move(right_pointer),
                                      *result, binary.span},
                          *result, span};
    }

    void add_missing_declarations() {
        const auto span = input_.characterization.span;
        if (!seen_reference_) {
            report(DiagnosticCode::MissingReference, "missing reference power declaration", span);
        }
        if (!seen_sweep_) {
            report(DiagnosticCode::MissingSweep, "missing sweep frequency declaration", span);
        }
        if (!seen_measurement_) {
            report(DiagnosticCode::MissingMeasurement, "missing measure power declaration", span);
        }
    }

    const ast::Program& input_;
    std::vector<Diagnostic> diagnostics_;
    std::unordered_map<std::string, Symbol> symbols_;
    std::unordered_set<std::string> declared_user_names_;
    std::unordered_set<std::string> future_derived_;
    std::optional<Symbol> reference_symbol_;
    std::optional<ReferencePower> reference_;
    std::optional<FrequencySweep> sweep_;
    std::optional<PowerMeasurement> measurement_;
    std::vector<DerivedQuantity> derived_;
    std::vector<Calibration> calibrations_;
    std::size_t next_symbol_{};
    bool seen_reference_{};
    bool seen_sweep_{};
    bool seen_measurement_{};
};

} // namespace

AnalysisResult analyze(const ast::Program& program) { return Analyzer(program).run(); }

} // namespace horusrf::semantic
