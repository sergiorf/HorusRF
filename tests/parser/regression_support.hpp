#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "horusrf/ast/ast.hpp"
#include "horusrf/parser/parser.hpp"

namespace horusrf::test::parser_regression {

struct Frontend {
    std::string_view name;
    std::vector<parser::Token> (*tokenize)(std::string_view);
    ast::Program (*parse)(std::string_view);
};

struct Observation {
    bool accepted{};
    std::string snapshot;

    friend bool operator==(const Observation&, const Observation&) = default;
};

[[nodiscard]] Frontend handwritten_frontend();
[[nodiscard]] std::string token_snapshot(const std::vector<parser::Token>& tokens);
[[nodiscard]] std::string ast_snapshot(const ast::Program& program);
[[nodiscard]] std::string diagnostic_snapshot(const parser::Diagnostic& diagnostic);
[[nodiscard]] Observation observe(const Frontend& frontend, std::string_view source);

// Migration milestones 4 and 5 can supply an ANTLR-backed Frontend and use this
// helper over the shared corpus. A mismatch includes both normalized observations.
void require_equivalent(const Frontend& expected, const Frontend& actual,
                        std::string_view case_name, std::string_view source);

} // namespace horusrf::test::parser_regression
