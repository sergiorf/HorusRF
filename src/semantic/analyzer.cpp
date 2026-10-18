#include "horusrf/semantic/analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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
    case DiagnosticCode::UnknownMeasurementOperation:
        return "semantic.unknown_measurement_operation";
    case DiagnosticCode::MissingCalibration: return "semantic.missing_calibration";
    case DiagnosticCode::UnknownIdentifier: return "semantic.unknown_identifier";
    case DiagnosticCode::UnknownReferenceMember: return "semantic.unknown_reference_member";
    case DiagnosticCode::ReferenceNotAvailable: return "semantic.reference_not_available";
    case DiagnosticCode::InvalidUnaryOperand: return "semantic.invalid_unary_operand";
    case DiagnosticCode::InvalidBinaryOperands: return "semantic.invalid_binary_operands";
    case DiagnosticCode::UnknownFunction: return "semantic.unknown_function";
    case DiagnosticCode::InvalidFunctionArgument: return "semantic.invalid_function_argument";
    case DiagnosticCode::InvalidComparisonOperands:
        return "semantic.invalid_comparison_operands";
    case DiagnosticCode::AssertionRequiresBoolean:
        return "semantic.assertion_requires_boolean";
    case DiagnosticCode::InvalidSweepRange: return "semantic.invalid_sweep_range";
    case DiagnosticCode::InvalidSweepStep: return "semantic.invalid_sweep_step";
    case DiagnosticCode::SweepTooLarge: return "semantic.sweep_too_large";
    case DiagnosticCode::UnknownCalibrationDimension:
        return "semantic.unknown_calibration_dimension";
    case DiagnosticCode::NonSweepCalibrationDimension:
        return "semantic.non_sweep_calibration_dimension";
    case DiagnosticCode::DuplicateCalibrationDimension:
        return "semantic.duplicate_calibration_dimension";
    case DiagnosticCode::InvalidCalibrationDimensions:
        return "semantic.invalid_calibration_dimensions";
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
    if (std::holds_alternative<domain::FrequencyDelta>(value)) return SemanticType::FrequencyDelta;
    if (std::holds_alternative<domain::Ratio>(value)) return SemanticType::Ratio;
    if (std::holds_alternative<domain::Temperature>(value)) return SemanticType::Temperature;
    if (std::holds_alternative<domain::TemperatureDelta>(value)) {
        return SemanticType::TemperatureDelta;
    }
    if (std::holds_alternative<domain::Power>(value)) return SemanticType::Power;
    return SemanticType::PowerDelta;
}

constexpr std::size_t maximum_sweep_points = 1'000'000;
constexpr long double sweep_tolerance_multiplier = 16.0L;

std::optional<std::size_t> axis_point_count(double start, double end, double step) {
    const auto first = static_cast<long double>(start);
    const auto last = static_cast<long double>(end);
    const auto increment = static_cast<long double>(step);
    const auto magnitude = std::max({std::abs(first), std::abs(last),
                                     std::abs(increment), 1.0L});
    const auto tolerance = sweep_tolerance_multiplier *
                           static_cast<long double>(std::numeric_limits<double>::epsilon()) *
                           magnitude;
    const auto quotient = (last - first) / increment;
    if (!std::isfinite(quotient) || quotient > static_cast<long double>(maximum_sweep_points)) {
        return std::nullopt;
    }
    auto last_index = std::floor(quotient);
    const auto candidate = first + last_index * increment;
    if (std::abs(candidate - last) > tolerance) {
        const auto next = first + (last_index + 1.0L) * increment;
        if (std::isfinite(next) && next <= last + tolerance) ++last_index;
    }
    const auto count = last_index + 1.0L;
    if (!std::isfinite(count) || count < 1.0L ||
        count > static_cast<long double>(maximum_sweep_points) ||
        count > static_cast<long double>(std::numeric_limits<std::size_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(count);
}

class Analyzer {
public:
    explicit Analyzer(const ast::Program& input) : input_(input) {
        for (const auto& statement : input_.characterization.statements) {
            if (const auto* derive = std::get_if<ast::DeriveStatement>(&statement)) {
                future_derived_.insert(derive->name);
            } else if (const auto* measurement =
                           std::get_if<ast::MeasurementStatement>(&statement)) {
                future_measurements_.insert(measurement->name);
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
                std::move(*reference_), std::move(*frequency_sweep_), std::move(sweeps_),
                total_point_count_, std::move(*measurement_),
                std::move(measurements_),
                std::move(derived_), std::move(assertions_), std::move(*calibration_)});
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
            case domain::QuantityConversionError::BelowAbsoluteZero:
                detail = "absolute temperature is below 0 K";
                break;
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
        if (seen_sweep_dimensions_.contains(statement.dimension)) {
            report(DiagnosticCode::DuplicateDeclaration,
                   "duplicate sweep " + statement.dimension + " declaration",
                   statement.span);
            return;
        }
        if (seen_any_measurement_) {
            report(DiagnosticCode::ReferenceNotAvailable,
                   "sweep declarations must precede measurements", statement.span);
            return;
        }
        seen_sweep_dimensions_.insert(statement.dimension);
        if (statement.dimension == "frequency") {
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
                report(DiagnosticCode::InvalidSweepRange,
                       "sweep end must not precede its start", statement.span);
                valid = false;
            }
            if (!valid) return;
            const auto count = axis_point_count(start->hertz(), end->hertz(), step->hertz());
            if (!count) {
                report(DiagnosticCode::SweepTooLarge,
                       "sweep point count exceeds the 1000000 point safety limit",
                       statement.span);
                return;
            }
            if (!add_sweep_points(*count, statement.span)) return;
            const auto id = next_symbol();
            FrequencySweep sweep{id, *start, *end, *step, *count, statement.span,
                                 statement.start.unit};
            frequency_sweep_ = sweep;
            sweeps_.push_back(sweep);
            symbols_.emplace("frequency", Symbol{id, SemanticType::Frequency, true, true});
            return;
        }

        auto start = quantity_as<domain::Temperature>(statement.start,
                                                      SemanticType::Temperature);
        auto end = quantity_as<domain::Temperature>(statement.end,
                                                    SemanticType::Temperature);
        auto step = quantity_as<domain::TemperatureDelta>(statement.step,
                                                          SemanticType::TemperatureDelta);
        if (!start || !end || !step) return;
        if (step->kelvin() <= 0.0) {
            report(DiagnosticCode::InvalidSweepStep, "sweep step must be strictly positive",
                   statement.step.span);
            return;
        }
        if (end->kelvin() < start->kelvin()) {
            report(DiagnosticCode::InvalidSweepRange,
                   "sweep end must not precede its start", statement.span);
            return;
        }
        const auto count = axis_point_count(start->kelvin(), end->kelvin(), step->kelvin());
        if (!count) {
            report(DiagnosticCode::SweepTooLarge,
                   "sweep point count exceeds the 1000000 point safety limit", statement.span);
            return;
        }
        if (!add_sweep_points(*count, statement.span)) return;
        const auto id = next_symbol();
        sweeps_.push_back(TemperatureSweep{id, *start, *end, *step, *count,
                                           statement.span, statement.start.unit});
        symbols_.emplace("temperature", Symbol{id, SemanticType::Temperature, true, true});
    }

    bool add_sweep_points(std::size_t count, parser::SourceSpan span) {
        if (count > maximum_sweep_points ||
            total_point_count_ > maximum_sweep_points / count) {
            report(DiagnosticCode::SweepTooLarge,
                   "sweep point count exceeds the 1000000 point safety limit", span);
            return false;
        }
        total_point_count_ *= count;
        return true;
    }

    void analyze_statement(const ast::MeasurementStatement& statement) {
        future_measurements_.erase(statement.name);
        seen_any_measurement_ = true;
        if (!seen_reference_) {
            report(DiagnosticCode::ReferenceNotAvailable,
                   "reference power must precede measurements", statement.span);
        }
        const bool name_valid = statement.legacy
                                    ? !symbols_.contains("power")
                                    : user_name_available(statement.name, statement.span);
        if (!name_valid) {
            if (statement.legacy) {
                report(DiagnosticCode::DuplicateDeclaration,
                       "duplicate declaration of 'power'", statement.span);
            }
            return;
        }
        MeasurementOperation operation{};
        SemanticType type{};
        if (statement.operation == "power") {
            operation = MeasurementOperation::Power;
            type = SemanticType::Power;
        } else if (statement.operation == "evm") {
            operation = MeasurementOperation::Evm;
            type = SemanticType::Ratio;
        } else if (statement.operation == "frequency_error") {
            operation = MeasurementOperation::FrequencyError;
            type = SemanticType::FrequencyDelta;
        } else {
            report(DiagnosticCode::UnknownMeasurementOperation,
                   "unknown measurement operation '" + statement.operation + "'",
                   statement.span);
            return;
        }
        const auto id = next_symbol();
        measurements_.push_back(Measurement{id, statement.name, operation, type,
                                            statement.legacy, statement.span});
        symbols_.emplace(statement.name, Symbol{id, type, false, true});
        if (operation == MeasurementOperation::Power && !measurement_) {
            measurement_.emplace(PowerMeasurement{id, statement.span});
        }
    }

    bool user_name_available(const std::string& name, parser::SourceSpan span) {
        if (name == "frequency" || name == "temperature" || name == "power" ||
            name == "abs" ||
            declared_user_names_.contains(name)) {
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

    void analyze_statement(const ast::AssertionStatement& statement) {
        auto expression = analyze_expression(statement.expression);
        if (!expression) return;
        if (expression->type != SemanticType::Boolean) {
            report(DiagnosticCode::AssertionRequiresBoolean,
                   "assert expression must have type Boolean, got " +
                       std::string(type_name(expression->type)), statement.expression.span);
            return;
        }
        const auto ordinal = assertions_.size() + 1;
        assertions_.push_back(Assertion{"assertion-" + std::to_string(ordinal), ordinal,
                                        std::move(*expression), statement.expression.span});
    }

    void analyze_statement(const ast::CalibrationStatement& statement) {
        if (seen_calibration_) {
            report(DiagnosticCode::DuplicateDeclaration, "duplicate calibration declaration",
                   statement.span);
            return;
        }
        seen_calibration_ = true;
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

        if (indexes.size() == sweeps_.size()) {
            for (std::size_t i = 0; i < indexes.size(); ++i) {
                const auto expected = std::visit([](const auto& sweep) { return sweep.symbol; },
                                                 sweeps_[i]);
                if (indexes[i] != expected) {
                    report(DiagnosticCode::InvalidCalibrationDimensions,
                           "calibration dimensions must list every sweep in declaration order",
                           statement.span);
                    valid = false;
                    break;
                }
            }
        } else {
            report(DiagnosticCode::InvalidCalibrationDimensions,
                   "calibration dimensions must list every sweep in declaration order",
                   statement.span);
            valid = false;
        }

        if (!valid) return;
        const auto id = next_symbol();
        calibration_.emplace(Calibration{
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
            future_derived_.contains(identifier.name) ||
            future_measurements_.contains(identifier.name)) {
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
        if (operand->type != SemanticType::PowerDelta &&
            operand->type != SemanticType::FrequencyDelta &&
            operand->type != SemanticType::Ratio &&
            operand->type != SemanticType::TemperatureDelta) {
            report(DiagnosticCode::InvalidUnaryOperand,
                   "cannot negate " + std::string(type_name(operand->type)), unary.span);
            return std::nullopt;
        }
        const auto type = operand->type;
        auto pointer = std::make_shared<Expression>(std::move(*operand));
        return Expression{UnaryValue{UnaryOperator::Negate, std::move(pointer),
                                     type, unary.span},
                          type, span};
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
            } else if (left->type == right->type &&
                       (left->type == SemanticType::FrequencyDelta ||
                        left->type == SemanticType::Ratio ||
                        left->type == SemanticType::TemperatureDelta)) {
                result = left->type;
            }
        } else if (left->type == SemanticType::Power && right->type == SemanticType::Power) {
            result = SemanticType::PowerDelta;
        } else if (left->type == right->type &&
                   (left->type == SemanticType::FrequencyDelta ||
                    left->type == SemanticType::Ratio ||
                    left->type == SemanticType::TemperatureDelta)) {
            result = left->type;
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

    std::optional<Expression> analyze_expression_value(const ast::CallExpression& call,
                                                       parser::SourceSpan span) {
        if (call.function != "abs") {
            report(DiagnosticCode::UnknownFunction,
                   "unknown function '" + call.function + "'", call.span);
            return std::nullopt;
        }
        if (!call.argument) return std::nullopt;
        auto argument = analyze_expression(*call.argument);
        if (!argument) return std::nullopt;
        if (argument->type != SemanticType::PowerDelta &&
            argument->type != SemanticType::FrequencyDelta &&
            argument->type != SemanticType::TemperatureDelta &&
            argument->type != SemanticType::Ratio) {
            report(DiagnosticCode::InvalidFunctionArgument,
                   "abs() does not accept " + std::string(type_name(argument->type)),
                   call.span);
            return std::nullopt;
        }
        const auto type = argument->type;
        return Expression{CallValue{"abs", std::make_shared<Expression>(std::move(*argument)),
                                    type, call.span}, type, span};
    }

    static bool contextualize_frequency_delta(Expression& expression) {
        auto* quantity = std::get_if<QuantityValue>(&expression.value);
        if (!quantity) return false;
        auto* frequency = std::get_if<domain::Frequency>(&quantity->value);
        if (!frequency) return false;
        quantity->value = domain::FrequencyDelta::from_hertz(frequency->hertz());
        expression.type = SemanticType::FrequencyDelta;
        return true;
    }

    std::optional<Expression> analyze_expression_value(
        const ast::ComparisonExpression& comparison, parser::SourceSpan span) {
        if (!comparison.left || !comparison.right) return std::nullopt;
        auto left = analyze_expression(*comparison.left);
        auto right = analyze_expression(*comparison.right);
        if (!left || !right) return std::nullopt;
        if (left->type == SemanticType::FrequencyDelta &&
            right->type == SemanticType::Frequency) contextualize_frequency_delta(*right);
        if (right->type == SemanticType::FrequencyDelta &&
            left->type == SemanticType::Frequency) contextualize_frequency_delta(*left);
        if (left->type != right->type || left->type == SemanticType::Boolean) {
            report(DiagnosticCode::InvalidComparisonOperands,
                   "cannot compare " + std::string(type_name(left->type)) + " and " +
                       type_name(right->type), comparison.span);
            return std::nullopt;
        }
        ComparisonOperator op = ComparisonOperator::Equal;
        switch (comparison.op) {
        case ast::ComparisonOperator::Less: op = ComparisonOperator::Less; break;
        case ast::ComparisonOperator::LessEqual: op = ComparisonOperator::LessEqual; break;
        case ast::ComparisonOperator::Greater: op = ComparisonOperator::Greater; break;
        case ast::ComparisonOperator::GreaterEqual: op = ComparisonOperator::GreaterEqual; break;
        case ast::ComparisonOperator::NotEqual: op = ComparisonOperator::NotEqual; break;
        default: break;
        }
        return Expression{ComparisonValue{
            op, std::make_shared<Expression>(std::move(*left)),
            std::make_shared<Expression>(std::move(*right)), comparison.span},
            SemanticType::Boolean, span};
    }

    void add_missing_declarations() {
        const auto span = input_.characterization.span;
        if (!seen_reference_) {
            report(DiagnosticCode::MissingReference, "missing reference power declaration", span);
        }
        if (!seen_sweep_dimensions_.contains("frequency")) {
            report(DiagnosticCode::MissingSweep, "missing sweep frequency declaration", span);
        }
        if (!measurement_) {
            report(DiagnosticCode::MissingMeasurement, "missing power measurement declaration", span);
        }
        if (!seen_calibration_) {
            report(DiagnosticCode::MissingCalibration, "missing calibration declaration", span);
        }
    }

    const ast::Program& input_;
    std::vector<Diagnostic> diagnostics_;
    std::unordered_map<std::string, Symbol> symbols_;
    std::unordered_set<std::string> declared_user_names_;
    std::unordered_set<std::string> future_derived_;
    std::unordered_set<std::string> future_measurements_;
    std::optional<Symbol> reference_symbol_;
    std::optional<ReferencePower> reference_;
    std::optional<FrequencySweep> frequency_sweep_;
    std::vector<Sweep> sweeps_;
    std::size_t total_point_count_{1};
    std::optional<PowerMeasurement> measurement_;
    std::vector<Measurement> measurements_;
    std::vector<DerivedQuantity> derived_;
    std::vector<Assertion> assertions_;
    std::optional<Calibration> calibration_;
    std::size_t next_symbol_{};
    bool seen_reference_{};
    std::unordered_set<std::string> seen_sweep_dimensions_;
    bool seen_any_measurement_{};
    bool seen_calibration_{};
};

} // namespace

AnalysisResult analyze(const ast::Program& program) { return Analyzer(program).run(); }

} // namespace horusrf::semantic
