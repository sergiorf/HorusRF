#include "test_support.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "horusrf/parser/lexer.hpp"
#include "regression_corpus.hpp"
#include "regression_support.hpp"

namespace {

using namespace horusrf;
using namespace horusrf::test::parser_regression;

constexpr std::string_view token_source =
    "characterize reference power sweep frequency temperature step measure derive calibration "
    "correction over assert name_1\r\n\tHz kHz MHz GHz dBm dB K degC deltaK deltaC % "
    "{} = == != < <= > >= .. . + - ( ) , // comment\r\n42 2.40 12abc .5 @ 1Hz";

constexpr std::string_view expected_token_snapshot = R"snapshot(characterize|characterize|[1:1@0-1:13@12]
reference|reference|[1:14@13-1:23@22]
power|power|[1:24@23-1:29@28]
sweep|sweep|[1:30@29-1:35@34]
frequency|frequency|[1:36@35-1:45@44]
temperature|temperature|[1:46@45-1:57@56]
step|step|[1:58@57-1:62@61]
measure|measure|[1:63@62-1:70@69]
derive|derive|[1:71@70-1:77@76]
calibration|calibration|[1:78@77-1:89@88]
correction|correction|[1:90@89-1:100@99]
over|over|[1:101@100-1:105@104]
assert|assert|[1:106@105-1:112@111]
identifier|name_1|[1:113@112-1:119@118]
Hz|Hz|[2:2@121-2:4@123]
kHz|kHz|[2:5@124-2:8@127]
MHz|MHz|[2:9@128-2:12@131]
GHz|GHz|[2:13@132-2:16@135]
dBm|dBm|[2:17@136-2:20@139]
dB|dB|[2:21@140-2:23@142]
K|K|[2:24@143-2:25@144]
degC|degC|[2:26@145-2:30@149]
deltaK|deltaK|[2:31@150-2:37@156]
deltaC|deltaC|[2:38@157-2:44@163]
%|%|[2:45@164-2:46@165]
{|{|[2:47@166-2:48@167]
}|}|[2:48@167-2:49@168]
=|=|[2:50@169-2:51@170]
==|==|[2:52@171-2:54@173]
!=|!=|[2:55@174-2:57@176]
<|<|[2:58@177-2:59@178]
<=|<=|[2:60@179-2:62@181]
>|>|[2:63@182-2:64@183]
>=|>=|[2:65@184-2:67@186]
..|..|[2:68@187-2:70@189]
.|.|[2:71@190-2:72@191]
+|+|[2:73@192-2:74@193]
-|-|[2:75@194-2:76@195]
(|(|[2:77@196-2:78@197]
)|)|[2:79@198-2:80@199]
,|,|[2:81@200-2:82@201]
number|42|[3:1@214-3:3@216]
number|2.40|[3:4@217-3:8@221]
invalid token|12abc|[3:9@222-3:14@227]
invalid token|.5|[3:15@228-3:17@230]
invalid token|@|[3:18@231-3:19@232]
invalid token|1Hz|[3:20@233-3:23@236]
end of file||[3:23@236-3:23@236]
)snapshot";

constexpr std::string_view ast_source =
    "characterize full {\n"
    "reference power = -10 dBm\n"
    "sweep frequency 1 Hz .. 2 GHz step 3 MHz\n"
    "measure power\n"
    "measure named = evm\n"
    "derive calc = -a + b - (reference.power + abs(1 dB))\n"
    "assert a < b\n"
    "assert a <= b\n"
    "assert a > b\n"
    "assert a >= b\n"
    "assert a == b\n"
    "assert a != b\n"
    "derive calibration cal { correction = calc over frequency, temperature }\n"
    "}";

constexpr std::string_view expected_ast_snapshot = R"snapshot(program[1:1@0-14:2@330]
characterization name=full[1:1@0-14:2@330]
0:reference power=-10 dBm[2:19@38-2:26@45] statement[2:1@20-2:26@45]
1:sweep frequency start=1 Hz[3:17@62-3:21@66] end=2 GHz[3:25@70-3:30@75] step=3 MHz[3:36@81-3:41@86] statement[3:1@46-3:41@86]
2:measurement name=power operation=power legacy=true statement[4:1@87-4:14@100]
3:measurement name=named operation=evm legacy=false statement[5:1@101-5:20@120]
4:derive name=calc value=binary(subtract,binary(add,unary(negate,identifier(a)[6:16@136-6:17@137] expression[6:16@136-6:17@137])[6:15@135-6:17@137] expression[6:15@135-6:17@137],identifier(b)[6:20@140-6:21@141] expression[6:20@140-6:21@141])[6:15@135-6:21@141] expression[6:15@135-6:21@141],binary(add,reference(reference.power)[6:25@145-6:40@160] expression[6:25@145-6:40@160],call(abs,quantity(1,dB)[6:47@167-6:51@171] expression[6:47@167-6:51@171])[6:43@163-6:52@172] expression[6:43@163-6:52@172])[6:25@145-6:52@172] expression[6:24@144-6:53@173])[6:15@135-6:53@173] expression[6:15@135-6:53@173] statement[6:1@121-6:53@173]
5:assert value=comparison(less,identifier(a)[7:8@181-7:9@182] expression[7:8@181-7:9@182],identifier(b)[7:12@185-7:13@186] expression[7:12@185-7:13@186])[7:8@181-7:13@186] expression[7:8@181-7:13@186] statement[7:1@174-7:13@186]
6:assert value=comparison(less_equal,identifier(a)[8:8@194-8:9@195] expression[8:8@194-8:9@195],identifier(b)[8:13@199-8:14@200] expression[8:13@199-8:14@200])[8:8@194-8:14@200] expression[8:8@194-8:14@200] statement[8:1@187-8:14@200]
7:assert value=comparison(greater,identifier(a)[9:8@208-9:9@209] expression[9:8@208-9:9@209],identifier(b)[9:12@212-9:13@213] expression[9:12@212-9:13@213])[9:8@208-9:13@213] expression[9:8@208-9:13@213] statement[9:1@201-9:13@213]
8:assert value=comparison(greater_equal,identifier(a)[10:8@221-10:9@222] expression[10:8@221-10:9@222],identifier(b)[10:13@226-10:14@227] expression[10:13@226-10:14@227])[10:8@221-10:14@227] expression[10:8@221-10:14@227] statement[10:1@214-10:14@227]
9:assert value=comparison(equal,identifier(a)[11:8@235-11:9@236] expression[11:8@235-11:9@236],identifier(b)[11:13@240-11:14@241] expression[11:13@240-11:14@241])[11:8@235-11:14@241] expression[11:8@235-11:14@241] statement[11:1@228-11:14@241]
10:assert value=comparison(not_equal,identifier(a)[12:8@249-12:9@250] expression[12:8@249-12:9@250],identifier(b)[12:13@254-12:14@255] expression[12:13@254-12:14@255])[12:8@249-12:14@255] expression[12:8@249-12:14@255] statement[12:1@242-12:14@255]
11:calibration name=cal correction=identifier(calc)[13:39@294-13:43@298] expression[13:39@294-13:43@298] dimensions=frequency,temperature statement[13:1@256-13:73@328]
)snapshot";

parser::Diagnostic parse_failure(std::string_view source) {
    try {
        (void)parser::parse(source);
    } catch (const parser::ParseError& error) {
        return error.diagnostic();
    }
    throw std::runtime_error("expected parsing to fail");
}

void token_regression_test() {
    const auto actual = token_snapshot(parser::lex(token_source));
    if (actual != expected_token_snapshot) {
        throw std::runtime_error("token snapshot mismatch:\n" + actual);
    }

    constexpr std::string_view all_ascii_whitespace = "a \t\v\f\r\nb";
    constexpr std::string_view expected_whitespace_snapshot =
        "identifier|a|[1:1@0-1:2@1]\n"
        "identifier|b|[2:1@7-2:2@8]\n"
        "end of file||[2:2@8-2:2@8]\n";
    CHECK_EQ(token_snapshot(parser::lex(all_ascii_whitespace)),
             expected_whitespace_snapshot);

    constexpr std::string_view malformed_numbers = "2. .5 1.2.3 12abc 1Hz";
    constexpr std::string_view expected_malformed_snapshot =
        "invalid token|2.|[1:1@0-1:3@2]\n"
        "invalid token|.5|[1:4@3-1:6@5]\n"
        "invalid token|1.2.3|[1:7@6-1:12@11]\n"
        "invalid token|12abc|[1:13@12-1:18@17]\n"
        "invalid token|1Hz|[1:19@18-1:22@21]\n"
        "end of file||[1:22@21-1:22@21]\n";
    CHECK_EQ(token_snapshot(parser::lex(malformed_numbers)),
             expected_malformed_snapshot);
}

void ast_regression_test() {
    const auto actual = ast_snapshot(parser::parse(ast_source));
    if (actual != expected_ast_snapshot) {
        throw std::runtime_error("AST snapshot mismatch:\n" + actual);
    }
}

void diagnostic_regression_test() {
    struct Case {
        std::string_view source;
        parser::DiagnosticCode code;
        parser::SourceSpan span;
    };
    constexpr std::array cases{
        Case{"characterize c { bogus }", parser::DiagnosticCode::UnexpectedToken,
             {{1, 18, 17}, {1, 23, 22}}},
        Case{"characterize c {", parser::DiagnosticCode::UnexpectedEndOfFile,
             {{1, 17, 16}, {1, 17, 16}}},
        Case{"characterize c { measure @ }", parser::DiagnosticCode::InvalidToken,
             {{1, 26, 25}, {1, 27, 26}}},
        Case{"characterize power {}", parser::DiagnosticCode::ExpectedIdentifier,
             {{1, 14, 13}, {1, 19, 18}}},
        Case{"characterize c { reference power = dBm }", parser::DiagnosticCode::ExpectedQuantity,
             {{1, 36, 35}, {1, 39, 38}}},
        Case{"characterize c { reference power = 10 }", parser::DiagnosticCode::ExpectedUnit,
             {{1, 39, 38}, {1, 40, 39}}},
        Case{"characterize c { derive x = }", parser::DiagnosticCode::ExpectedExpression,
             {{1, 29, 28}, {1, 30, 29}}},
    };

    for (std::size_t index = 0; index < cases.size(); ++index) {
        const auto& test_case = cases[index];
        const auto diagnostic = parse_failure(test_case.source);
        CHECK_EQ(diagnostic.code, test_case.code);
        if (diagnostic.span != test_case.span) {
            throw std::runtime_error("diagnostic span mismatch at case " +
                                     std::to_string(index) + ": " +
                                     diagnostic_snapshot(diagnostic));
        }
    }
}

void shared_adapter_test() {
    const auto frontend = handwritten_frontend();
    for (const auto source : valid_sources) {
        require_equivalent(frontend, frontend, "valid corpus", source);
    }
    for (const auto source : invalid_sources) {
        require_equivalent(frontend, frontend, "invalid corpus", source);
    }
}

} // namespace

int main() {
    try {
        token_regression_test();
        ast_regression_test();
        diagnostic_regression_test();
        shared_adapter_test();
        std::cout << "All parser migration regression tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
