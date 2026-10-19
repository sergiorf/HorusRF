#include "HorusRfAntlrSmokeLexer.h"
#include "HorusRfAntlrSmokeParser.h"

#include <antlr4-runtime.h>

#include <iostream>
#include <string>

int main() {
    antlr4::ANTLRInputStream input{"horusrf"};
    HorusRfAntlrSmokeLexer lexer{&input};
    antlr4::CommonTokenStream tokens{&lexer};
    HorusRfAntlrSmokeParser parser{&tokens};

    auto* document = parser.document();
    if (lexer.getNumberOfSyntaxErrors() != 0 ||
        parser.getNumberOfSyntaxErrors() != 0 ||
        document->getText() != "horusrf<EOF>") {
        std::cerr << "ANTLR toolchain smoke parse failed\n";
        return 1;
    }
    return 0;
}
