#include "CompletionContext.h"

#include "Cosmos/Properties.h"
#include "Cosmos/Tokenizer.h"

namespace CosmosLsp {

namespace {

std::vector<Cosmos::Token> TokenizeAll(std::string_view Source) {
    Cosmos::Tokenizer          Tok(Source, "");
    std::vector<Cosmos::Token> Tokens;
    for (;;) {
        Cosmos::Token T = Tok.NextToken();
        const bool IsEof = T.Kind == Cosmos::TokenKind::EndOfFile;
        Tokens.push_back(std::move(T));
        if (IsEof) {
            break;
        }
    }
    return Tokens;
}

// How many source columns a token occupies, including the punctuation
// Cosmos::Token::Text itself doesn't carry (ClassName/PseudoClass/VariableName strip
// their leading `.`/`:`/`--`, StringLiteral strips its quotes — see Tokenizer.cpp).
std::size_t TokenDisplayLength(const Cosmos::Token& T) {
    switch (T.Kind) {
    case Cosmos::TokenKind::ClassName:
    case Cosmos::TokenKind::PseudoClass: return T.Text.size() + 1;
    case Cosmos::TokenKind::VariableName: return T.Text.size() + 2;
    case Cosmos::TokenKind::StringLiteral: return T.Text.size() + 2;
    default: return T.Text.size();
    }
}

bool StartsAtOrBefore(const Cosmos::SourceLocation& Loc, std::uint32_t Line, std::uint32_t Column) {
    return Loc.Line < Line || (Loc.Line == Line && Loc.Column <= Column);
}

} // namespace

CompletionContext ClassifyCompletionContext(std::string_view Source, std::uint32_t Line, std::uint32_t Column) {
    CompletionContext State;

    std::string LastIdentifierText;
    bool        JustSawVarKeyword = false;
    int         VarParenDepth = 0;

    for (const Cosmos::Token& T : TokenizeAll(Source)) {
        if (T.Kind == Cosmos::TokenKind::EndOfFile || !StartsAtOrBefore(T.Location, Line, Column)) {
            break;
        }

        switch (T.Kind) {
        case Cosmos::TokenKind::OpenBrace:
            ++State.Depth;
            State.Kind = CompletionContextKind::Statement;
            VarParenDepth = 0;
            break;
        case Cosmos::TokenKind::CloseBrace:
            State.Depth = State.Depth > 0 ? State.Depth - 1 : 0;
            State.Kind = CompletionContextKind::Statement;
            VarParenDepth = 0;
            break;
        case Cosmos::TokenKind::Semicolon:
            State.Kind = CompletionContextKind::Statement;
            VarParenDepth = 0;
            break;
        case Cosmos::TokenKind::Colon:
            // A pseudo-class colon (`:hover`) is lexed as its own PseudoClass token
            // (Tokenizer.cpp's NextToken — a bare Colon token is only ever a
            // property/variable declaration's separator), so this unconditionally
            // means "value follows".
            if (State.Kind == CompletionContextKind::Statement) {
                State.Kind = CompletionContextKind::Value;
                State.Property = LastIdentifierText;
            }
            break;
        case Cosmos::TokenKind::VarKeyword: JustSawVarKeyword = true; continue;
        case Cosmos::TokenKind::OpenParen:
            if (JustSawVarKeyword) {
                ++VarParenDepth;
            }
            break;
        case Cosmos::TokenKind::CloseParen:
            if (VarParenDepth > 0) {
                --VarParenDepth;
            }
            break;
        case Cosmos::TokenKind::Identifier:
        case Cosmos::TokenKind::RootKeyword: LastIdentifierText = T.Text; break;
        default: break;
        }
        JustSawVarKeyword = false;
    }

    if (VarParenDepth > 0) {
        State.Kind = CompletionContextKind::VarRef;
    }
    return State;
}

std::vector<Cosmos::Token> TokenizeDocument(std::string_view Source) { return TokenizeAll(Source); }

std::optional<Cosmos::Token> TokenAtPosition(std::string_view Source, std::uint32_t Line, std::uint32_t Column) {
    for (const Cosmos::Token& T : TokenizeAll(Source)) {
        if (T.Kind == Cosmos::TokenKind::EndOfFile) {
            break;
        }
        if (T.Location.Line != Line) {
            continue;
        }
        const std::uint32_t End = T.Location.Column + static_cast<std::uint32_t>(TokenDisplayLength(T));
        if (Column >= T.Location.Column && Column <= End) {
            return T;
        }
    }
    return std::nullopt;
}

std::vector<const Cosmos::VariableDeclaration*> CollectInScopeVariables(const Cosmos::Stylesheet& Sheet) {
    std::vector<const Cosmos::VariableDeclaration*> Result;
    if (Sheet.Root) {
        for (const auto& V : Sheet.Root->Variables) {
            Result.push_back(&V);
        }
    }
    for (const auto& Rule : Sheet.Rules) {
        for (const auto& V : Rule->Variables) {
            Result.push_back(&V);
        }
    }
    return Result;
}

const std::vector<std::string_view> kPropertyNames = Cosmos::PropertyNames();

const std::vector<std::string_view> kPrimitiveSelectorNames{
    "frame",    "inline",   "grid",      "image",      "text",   "scroll", "input",
    "textarea", "barchart", "linechart", "donutchart", "series", "slice",
};

const std::vector<std::string_view> kPseudoClassNames = [] {
    std::vector<std::string_view> Names;
    for (const Cosmos::PseudoClassInfo& PseudoClass : Cosmos::AllPseudoClasses()) {
        Names.push_back(PseudoClass.Name);
    }
    return Names;
}();

std::vector<std::string_view> PropertyValueKeywords(std::string_view Property) {
    const Cosmos::PropertyInfo* Info = Cosmos::FindProperty(Property);
    return Info ? std::vector<std::string_view>(Info->Keywords.begin(), Info->Keywords.end())
                : std::vector<std::string_view>{};
}

} // namespace CosmosLsp
