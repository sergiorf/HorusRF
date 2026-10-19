#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "horusrf/ast/ast.hpp"
#include "horusrf/parser/token.hpp"

namespace horusrf::parser {

enum class DiagnosticCode {
    UnexpectedToken,
    UnexpectedEndOfFile,
    InvalidToken,
    ExpectedIdentifier,
    ExpectedQuantity,
    ExpectedUnit,
    ExpectedExpression
};

[[nodiscard]] std::string_view diagnostic_code_name(DiagnosticCode code) noexcept;

struct Diagnostic {
    DiagnosticCode code{DiagnosticCode::UnexpectedToken};
    std::string message;
    SourceSpan span;
};

class ParseError : public std::runtime_error {
public:
    explicit ParseError(Diagnostic diagnostic);
    [[nodiscard]] const Diagnostic& diagnostic() const noexcept { return diagnostic_; }

private:
    Diagnostic diagnostic_;
};

[[nodiscard]] ast::Program parse(std::string_view source);

} // namespace horusrf::parser
