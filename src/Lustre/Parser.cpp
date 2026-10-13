#include "Lustre/Parser.h"

#include <array>
#include <utility>

namespace Lustre {

namespace {
bool IsPrimitiveName(std::string_view Name) {
    static constexpr std::array<std::string_view, 8> kPrimitives{"frame", "inline", "grid",  "image",
                                                                   "text",  "scroll", "input", "textarea"};
    for (const auto& P : kPrimitives) {
        if (P == Name) {
            return true;
        }
    }
    return false;
}

std::optional<PseudoKind> ParsePseudoKind(std::string_view Name) {
    if (Name == "hover") {
        return PseudoKind::Hover;
    }
    if (Name == "active") {
        return PseudoKind::Active;
    }
    if (Name == "disabled") {
        return PseudoKind::Disabled;
    }
    return std::nullopt;
}

std::string_view PseudoKindName(PseudoKind Kind) {
    switch (Kind) {
    case PseudoKind::Hover: return "hover";
    case PseudoKind::Active: return "active";
    case PseudoKind::Disabled: return "disabled";
    }
    return "";
}
} // namespace

Parser::Parser(std::string_view Source, std::string FilePath)
    : Tokenizer_(Source, FilePath), FilePath_(std::move(FilePath)) {
    Current_ = Tokenizer_.NextToken();
    Peek_ = Tokenizer_.NextToken();
}

void Parser::Advance() {
    Current_ = Peek_;
    Peek_ = Tokenizer_.NextToken();
}

bool Parser::Check(TokenKind Kind) const { return Current_.Kind == Kind; }

bool Parser::Match(TokenKind Kind) {
    if (Check(Kind)) {
        Advance();
        return true;
    }
    return false;
}

Token Parser::Expect(TokenKind Kind, std::string_view MessageIfMissing) {
    if (Check(Kind)) {
        Token Result = Current_;
        Advance();
        return Result;
    }
    Errors_.push_back(ParseError{std::string(MessageIfMissing), Current_.Location});
    return Current_;
}

std::string Parser::DescribeSelector(const Rule& R) {
    std::string Text = R.Kind == SelectorKind::Class ? ("." + R.ClassName) : R.PrimitiveName;
    if (R.Pseudo.has_value()) {
        Text += ":";
        Text += PseudoKindName(*R.Pseudo);
    }
    return Text;
}

void Parser::CheckDuplicateSelectors(const std::vector<RulePtr>& Rules) {
    for (std::size_t I = 0; I < Rules.size(); ++I) {
        for (std::size_t J = I + 1; J < Rules.size(); ++J) {
            if (DescribeSelector(*Rules[I]) == DescribeSelector(*Rules[J])) {
                Errors_.push_back(ParseError{
                    "Duplicate selector `" + DescribeSelector(*Rules[J]) + "` — two rule blocks with the "
                    "identical selector in one file.",
                    Rules[J]->Location});
            }
        }
    }
    for (const auto& R : Rules) {
        CheckDuplicateSelectors(R->NestedRules);
    }
}

std::optional<RootBlock> Parser::TryParseRootBlock() {
    RootBlock Block;
    Block.Location = Current_.Location;
    Advance(); // consume the ":root" pseudo-class-shaped token
    Expect(TokenKind::OpenBrace, "Expected `{` after `:root`.");
    while (!Check(TokenKind::CloseBrace) && !Check(TokenKind::EndOfFile)) {
        if (Check(TokenKind::VariableName)) {
            Token NameToken = Current_;
            Advance();
            Block.Variables.push_back(ParseVariableDeclaration(NameToken));
        } else {
            Errors_.push_back(ParseError{"Expected a `--variable: value;` declaration inside `:root { }`.", Current_.Location});
            Advance();
        }
    }
    Expect(TokenKind::CloseBrace, "Expected `}` to close `:root { }`.");
    return Block;
}

ValuePart Parser::ParseValuePart() {
    if (Check(TokenKind::VarKeyword)) {
        Advance();
        Expect(TokenKind::OpenParen, "Expected `(` after `var`.");
        Token NameToken = Expect(TokenKind::VariableName, "Expected `--variable-name` inside `var(...)`.");
        Expect(TokenKind::CloseParen, "Expected `)` to close `var(...)`.");
        return ValuePart{.VariableRef = NameToken.Text};
    }
    if (Check(TokenKind::Number)) {
        std::string Text = Current_.Text;
        Advance();
        return ValuePart{.Number = std::move(Text)};
    }
    if (Check(TokenKind::StringLiteral)) {
        std::string Text = Current_.Text;
        Advance();
        return ValuePart{.StringValue = std::move(Text)};
    }
    if (Check(TokenKind::HexColor)) {
        std::string Text = Current_.Text;
        Advance();
        return ValuePart{.HexColor = std::move(Text)};
    }
    if (Check(TokenKind::Identifier) || Check(TokenKind::RootKeyword)) {
        std::string Text = Current_.Text;
        Advance();
        if (Text == "attr" && Match(TokenKind::OpenParen)) {
            // CSS's attr(), minus a type or fallback: the value of one of the element's
            // own props, which only the backend knows.
            Token NameToken = Expect(TokenKind::Identifier, "Expected a prop name inside `attr(...)`.");
            Expect(TokenKind::CloseParen, "Expected `)` to close `attr(...)`.");
            return ValuePart{.AttrRef = NameToken.Text};
        }
        if (Match(TokenKind::OpenParen)) {
            // v1's only function-call value shape: `scale(1.5)` (§2's
            // `transform`), not a general function-call grammar.
            Token Arg = Expect(TokenKind::Number, "Expected a number inside `" + Text + "(...)`.");
            Expect(TokenKind::CloseParen, "Expected `)` to close `" + Text + "(...)`.");
            return ValuePart{.Literal = std::move(Text), .CallArgument = Arg.Text};
        }
        return ValuePart{.Literal = std::move(Text)};
    }
    Errors_.push_back(ParseError{"Expected a property value.", Current_.Location});
    Advance();
    return ValuePart{};
}

std::vector<ValuePart> Parser::ParseValueList() {
    std::vector<ValuePart> Values;
    bool                   AfterComma = false;
    while (!Check(TokenKind::Semicolon) && !Check(TokenKind::CloseBrace) && !Check(TokenKind::EndOfFile)) {
        if (Match(TokenKind::Comma)) {
            AfterComma = true;
            continue;
        }
        Values.push_back(ParseValuePart());
        Values.back().FollowsComma = AfterComma;
        AfterComma                 = false;
    }
    return Values;
}

Declaration Parser::ParseDeclaration(const Token& PropertyToken) {
    Declaration Decl;
    Decl.Property = PropertyToken.Text;
    Decl.Location = PropertyToken.Location;
    Expect(TokenKind::Colon, "Expected `:` after property name `" + PropertyToken.Text + "`.");
    Decl.Values = ParseValueList();
    Expect(TokenKind::Semicolon, "Expected `;` after declaration.");
    return Decl;
}

VariableDeclaration Parser::ParseVariableDeclaration(const Token& NameToken) {
    VariableDeclaration Decl;
    Decl.Name = NameToken.Text;
    Decl.Location = NameToken.Location;
    Expect(TokenKind::Colon, "Expected `:` after variable name `--" + NameToken.Text + "`.");
    Decl.Value = ParseValuePart();
    Expect(TokenKind::Semicolon, "Expected `;` after variable declaration.");
    return Decl;
}

void Parser::ParseRuleBody(Rule& Target, int ChildDepth) {
    Expect(TokenKind::OpenBrace, "Expected `{` to open the rule body.");
    while (!Check(TokenKind::CloseBrace) && !Check(TokenKind::EndOfFile)) {
        if (Check(TokenKind::VariableName)) {
            Token NameToken = Current_;
            Advance();
            Target.Variables.push_back(ParseVariableDeclaration(NameToken));
            continue;
        }
        if (Check(TokenKind::ClassName)) {
            if (RulePtr Nested = ParseRule(ChildDepth)) {
                Target.NestedRules.push_back(std::move(Nested));
            }
            continue;
        }
        if (Check(TokenKind::AtKeyword)) {
            ParseStartingStyle(Target);
            continue;
        }
        if (Check(TokenKind::Identifier)) {
            // Lookahead: `ident:` is a declaration; `ident{`/`ident:pseudo{`
            // is a nested primitive-selector rule.
            if (Peek_.Kind == TokenKind::Colon) {
                Token PropertyToken = Current_;
                Advance();
                Target.Declarations.push_back(ParseDeclaration(PropertyToken));
                continue;
            }
            if (Peek_.Kind == TokenKind::OpenBrace || Peek_.Kind == TokenKind::PseudoClass) {
                if (RulePtr Nested = ParseRule(ChildDepth)) {
                    Target.NestedRules.push_back(std::move(Nested));
                }
                continue;
            }
            Errors_.push_back(ParseError{"Unexpected token after `" + Current_.Text + "` — expected `:` (a declaration) or `{` (a nested rule).", Current_.Location});
            Advance();
            continue;
        }
        Errors_.push_back(ParseError{"Expected a declaration or a nested rule inside the block.", Current_.Location});
        Advance();
    }
    Expect(TokenKind::CloseBrace, "Expected `}` to close the rule body.");
}

void Parser::ParseStartingStyle(Rule& Target) {
    const Token AtToken = Current_;
    Advance();
    if (AtToken.Text != "starting-style") {
        Errors_.push_back(ParseError{"Unknown at-rule `@" + AtToken.Text + "` -- the only one is `@starting-style`.",
                                     AtToken.Location});
    }
    Expect(TokenKind::OpenBrace, "Expected `{` after `@" + AtToken.Text + "`.");
    while (!Check(TokenKind::CloseBrace) && !Check(TokenKind::EndOfFile)) {
        if (Check(TokenKind::Identifier) && Peek_.Kind == TokenKind::Colon) {
            Token PropertyToken = Current_;
            Advance();
            Declaration Decl = ParseDeclaration(PropertyToken);
            if (AtToken.Text == "starting-style") {
                Target.StartingDeclarations.push_back(std::move(Decl));
            }
            continue;
        }
        Errors_.push_back(ParseError{"Expected a declaration inside `@" + AtToken.Text + " { }` -- it holds "
                                     "property values only, no nested rules or variables.",
                                     Current_.Location});
        Advance();
    }
    Expect(TokenKind::CloseBrace, "Expected `}` to close `@" + AtToken.Text + " { }`.");
}

RulePtr Parser::ParseRule(int Depth) {
    auto R = std::make_unique<Rule>();
    R->Location = Current_.Location;
    R->Depth = Depth;
    R->SourceIndex = NextSourceIndex_++;

    if (Check(TokenKind::ClassName)) {
        R->Kind = SelectorKind::Class;
        R->ClassName = Current_.Text;
        Advance();
    } else if (Check(TokenKind::Identifier)) {
        const Token SelectorToken = Current_;
        Advance();
        if (Check(TokenKind::ClassName)) {
            // `Frame.card { }` — a compound tag+class selector, never valid
            // syntax (§1.1).
            Errors_.push_back(ParseError{
                "Compound tag+class selector `" + SelectorToken.Text + "." + Current_.Text +
                    "` is not valid Lustre syntax — selectors are never combined.",
                SelectorToken.Location});
            RecoverToNextRule();
            return nullptr;
        }
        if (!IsPrimitiveName(SelectorToken.Text)) {
            Errors_.push_back(ParseError{
                "Unknown primitive selector `" + SelectorToken.Text + "` — expected one of "
                "`frame`, `inline`, `grid`, `image`, `text`, or a `.class` selector.",
                SelectorToken.Location});
            RecoverToNextRule();
            return nullptr;
        }
        R->Kind = SelectorKind::Primitive;
        R->PrimitiveName = SelectorToken.Text;
    } else {
        Errors_.push_back(ParseError{"Expected a `.class` or primitive-element selector.", Current_.Location});
        RecoverToNextRule();
        return nullptr;
    }

    if (Check(TokenKind::PseudoClass)) {
        if (auto Kind = ParsePseudoKind(Current_.Text)) {
            R->Pseudo = *Kind;
            Advance();
        } else {
            Errors_.push_back(ParseError{
                "Unknown pseudo-class `:" + Current_.Text + "` — expected one of `:hover`, `:active`, `:disabled`.",
                Current_.Location});
            Advance();
        }
    }

    ParseRuleBody(*R, Depth + 1);
    return R;
}

void Parser::RecoverToNextRule() {
    // Skip tokens until something that could plausibly start the next
    // top-level (or sibling nested) rule, so one malformed selector doesn't
    // cascade into spurious errors for the rest of the file.
    while (!Check(TokenKind::EndOfFile) && !Check(TokenKind::ClassName) && !Check(TokenKind::Identifier) &&
           !Check(TokenKind::CloseBrace)) {
        Advance();
    }
}

ParseResult Parser::Parse() {
    Stylesheet Sheet;

    if (Check(TokenKind::PseudoClass) && Current_.Text == "root") {
        Sheet.Root = TryParseRootBlock();
    }

    while (!Check(TokenKind::EndOfFile)) {
        if (RulePtr R = ParseRule()) {
            Sheet.Rules.push_back(std::move(R));
        }
    }

    CheckDuplicateSelectors(Sheet.Rules);

    // The (possibly partial) tree is handed back regardless of errors, so a
    // caller can report every error at once rather than stop at the first.
    ParseResult Result;
    Result.Errors = std::move(Errors_);
    Result.Sheet = std::move(Sheet);
    return Result;
}

} // namespace Lustre
