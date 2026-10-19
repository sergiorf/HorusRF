#pragma once

#include <string>
#include <vector>

#include "horusrf/ast/ast.hpp"
#include "horusrf/parser/parser.hpp"

namespace horusrf::test::parser_regression {

[[nodiscard]] std::string token_snapshot(const std::vector<parser::Token>& tokens);
[[nodiscard]] std::string ast_snapshot(const ast::Program& program);
[[nodiscard]] std::string diagnostic_snapshot(const parser::Diagnostic& diagnostic);

} // namespace horusrf::test::parser_regression
