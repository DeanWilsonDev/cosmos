#include "CompletionContext.h"

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

const std::vector<std::string_view> kPropertyNames{
    "background-color", "background-gradient-start", "background-gradient-end",
    "border-color",     "border-width",               "border-radius",
    "padding",          "margin",                      "color",
    "font-family",      "font-size",                   "display",
    "flex-direction",   "gap",                         "align-items",
    "width",            "height",                      "min-width",
    "max-width",        "text-overflow",               "white-space",
    "transform",        "transition",                  "flex-grow",
    "scrollbar-color",  "scrollbar-width",             "font-style",
    "text-decoration",  "opacity",                     "axis-color",
    "axis-width",       "grid-color",                  "grid-width",
    "hover-color",      "tick-spacing",                "label-gap",
    "line-width",       "point-radius",                "hover-point-radius",
    "area-opacity",     "bar-gap",                     "bar-radius",
    "donut-thickness",  "segment-gap",                 "appear-duration",
    "change-duration",  "selection-color",             "accent-color",
    "position",
    "top",              "right",                       "bottom",
    "left",             "inset",                       "z-index",
    "pointer-events",
};

const std::vector<std::string_view> kPrimitiveSelectorNames{
    "frame",    "inline",   "grid",      "image",      "text",   "scroll", "input",
    "textarea", "barchart", "linechart", "donutchart", "series", "slice",
};

const std::vector<std::string_view> kPseudoClassNames{
    "hover",
    "active",
    "disabled",
};

std::vector<std::string_view> PropertyValueKeywords(std::string_view Property) {
    if (Property == "display") {
        return {"stack", "inline"};
    }
    if (Property == "flex-direction") {
        return {"row", "column"};
    }
    if (Property == "align-items") {
        return {"start", "center", "end", "stretch"};
    }
    if (Property == "text-overflow") {
        return {"clip", "ellipsis"};
    }
    if (Property == "white-space") {
        return {"nowrap", "normal"};
    }
    if (Property == "font-style") {
        return {"normal", "italic"};
    }
    if (Property == "text-decoration") {
        return {"none", "underline", "line-through"};
    }
    if (Property == "transition") {
        return {"linear", "ease", "ease-in", "ease-out", "ease-in-out"};
    }
    if (Property == "position") {
        return {"static", "relative", "absolute", "fixed", "sticky"};
    }
    if (Property == "pointer-events") {
        return {"auto", "none"};
    }
    if (Property == "top" || Property == "right" || Property == "bottom" || Property == "left" ||
        Property == "inset" || Property == "z-index") {
        return {"auto"};
    }
    return {};
}

} // namespace CosmosLsp
