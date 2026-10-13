#pragma once

#include "Lustre/Token.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace Lustre {

// Lexer for `.lustre` source (docs/lustre_core_spec.md §1). Understands the
// full v1 grammar's lexical surface: class/pseudo-class selectors, bare
// identifiers (primitive selectors, property names, keyword values),
// `--variable` declarations and `var(--name)` references, `:root`, numbers
// with an attached unit suffix folded into the token text (`16px`, `0.2s`,
// `100%`), string literals, hex colors, and `/* ... */` block comments (no
// line-comment form, per §1.6). Comments are consumed and never emitted as
// tokens — nothing downstream needs them.
class Tokenizer {
public:
    Tokenizer(std::string_view Source, std::string FilePath);

    Token NextToken();

private:
    bool AtEnd() const;
    char CurrentChar() const;
    char PeekChar(std::size_t Offset = 1) const;
    void Advance();
    void SkipWhitespaceAndComments();

    Token LexIdentifierLike();   // identifier, var/root keywords
    Token LexClassName();        // .foo
    Token LexPseudoClass();      // :hover  (vs. lone ':' before a value)
    Token LexAtKeyword();        // @starting-style
    Token LexVariableName();     // --foo
    Token LexNumber();           // 16px, 0.2s, 100%, 1.5
    Token LexStringLiteral();    // "..."
    Token LexHexColor();         // #RRGGBB / #RGB

    SourceLocation CurrentLocation() const;

    std::string_view Source_;
    std::string       FilePath_;
    std::size_t       Pos_{0};
    std::uint32_t     Line_{1};
    std::uint32_t     Column_{1};
};

} // namespace Lustre
