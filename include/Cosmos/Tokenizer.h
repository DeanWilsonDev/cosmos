#pragma once

#include "Cosmos/Token.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace Cosmos {

// Lexer for `.cosmos` source (docs/cosmos_core_spec.md §1). Understands the
// full v1 grammar's lexical surface: class/pseudo-class selectors, bare
// identifiers (primitive selectors, property names, keyword values),
// `--variable` declarations and `var(--name)` references, `:root`, numbers
// with an attached unit suffix folded into the token text (`16px`, `0.2s`,
// `100%`), string literals, hex colors, and `/* ... */` block comments (no
// line-comment form, per §1.6). Comments are skipped unless asked for.
enum class CommentHandling { Skip, Emit };

class Tokenizer {
public:
    // With CommentHandling::Emit, each comment comes back as a Comment token, for the
    // formatter; the parser always skips them.
    Tokenizer(std::string_view Source, std::string FilePath, CommentHandling Comments = CommentHandling::Skip);

    Token NextToken();

private:
    bool AtEnd() const;
    char CurrentChar() const;
    char PeekChar(std::size_t Offset = 1) const;
    void Advance();
    void SkipWhitespaceAndComments();
    Token LexToken();

    Token LexIdentifierLike();   // identifier, var/root keywords
    Token LexClassName();        // .foo
    Token LexPseudoClass();      // :hover  (vs. lone ':' before a value)
    Token LexAtKeyword();        // @starting-style
    Token LexVariableName();     // --foo
    Token LexNumber();           // 16px, 0.2s, 100%, 1.5
    Token LexStringLiteral();    // "..."
    Token LexHexColor();         // #RRGGBB / #RGB
    Token LexComment();          // /* ... */

    SourceLocation CurrentLocation() const;

    std::string_view Source_;
    std::string       FilePath_;
    CommentHandling   Comments_;
    std::size_t       Pos_{0};
    std::uint32_t     Line_{1};
    std::uint32_t     Column_{1};
};

} // namespace Cosmos
