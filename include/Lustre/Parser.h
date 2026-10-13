#pragma once

#include "Lustre/Ast.h"
#include "Lustre/SourceLocation.h"
#include "Lustre/Token.h"
#include "Lustre/Tokenizer.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Lustre {

struct ParseError {
    std::string    Message;
    SourceLocation Location;
};

struct ParseResult {
    std::optional<Stylesheet> Sheet;
    std::vector<ParseError>   Errors;
};

// Recursive-descent parser for one `.lustre` file (docs/lustre_core_spec.md
// §1). Diagnoses the two purely-syntactic errors from §6's catalogue —
// compound tag+class selectors and duplicate-selector rule blocks — as
// parse errors; everything else in §6 (undefined variables,
// component-boundary crossing) needs whole-project or whole-render-tree
// context this single-file parser doesn't have, and is left to the resolver.
class Parser {
public:
    Parser(std::string_view Source, std::string FilePath);

    ParseResult Parse();

private:
    void  Advance();
    bool  Check(TokenKind Kind) const;
    bool  Match(TokenKind Kind);
    Token Expect(TokenKind Kind, std::string_view MessageIfMissing);
    void  RecoverToNextRule();

    std::optional<RootBlock> TryParseRootBlock();
    RulePtr                  ParseRule(int Depth = 0);
    void                     ParseRuleBody(Rule& Target, int ChildDepth);
    void                     ParseStartingStyle(Rule& Target);
    Declaration               ParseDeclaration(const Token& PropertyToken);
    VariableDeclaration       ParseVariableDeclaration(const Token& NameToken);
    std::vector<ValuePart>    ParseValueList();
    ValuePart                 ParseValuePart();
    void CheckDuplicateSelectors(const std::vector<RulePtr>& Rules);
    static std::string DescribeSelector(const Rule& R);

    Tokenizer   Tokenizer_;
    std::string FilePath_;
    Token       Current_;
    Token       Peek_; // one-token lookahead, needed to tell a property
                        // declaration (`ident:`) apart from a nested
                        // primitive-selector rule (`ident{`/`ident:pseudo{`)
    std::vector<ParseError> Errors_;
    std::size_t              NextSourceIndex_{0};
};

} // namespace Lustre
