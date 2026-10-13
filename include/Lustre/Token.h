#pragma once

#include "Lustre/SourceLocation.h"

#include <string>

namespace Lustre {

enum class TokenKind {
    // .card, .card-title
    ClassName,
    // frame, inline, grid, image, text, display, background-color, ...
    // (bare identifiers; the parser decides selector-vs-property-vs-value
    // role from grammar position, same as CSS)
    Identifier,
    // :hover, :active, :disabled
    PseudoClass,
    // @starting-style (the name, without the @)
    AtKeyword,
    // --color-primary (declaration site) / var(--color-primary) (reference,
    // emitted as VarKeyword + OpenParen + VariableName + CloseParen)
    VariableName,
    VarKeyword,
    RootKeyword,
    // 16px, 1.5, 100%, 0.2s
    Number,
    // "assets/fonts/body.ttf"
    StringLiteral,
    // #E8593C
    HexColor,
    OpenBrace,
    CloseBrace,
    OpenParen,
    CloseParen,
    Colon,
    Semicolon,
    Comma,
    EndOfFile,
    Invalid,
};

struct Token {
    TokenKind      Kind{TokenKind::Invalid};
    std::string    Text;
    SourceLocation Location;
};

} // namespace Lustre
