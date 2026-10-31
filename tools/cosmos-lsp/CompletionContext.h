#pragma once

#include "cosmos/Ast.h"
#include "cosmos/Token.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CosmosLsp {

enum class CompletionContextKind {
    // Start of a new selector or declaration — a `.class`/primitive selector, a
    // pseudo-class, or (inside a block) a property name. Cosmos's own grammar makes a
    // property name and a nested primitive selector ambiguous until the *next* token
    // (`ident:` vs `ident{`), so this context offers both rather than trying to
    // disambiguate a partially-typed identifier.
    Statement,
    // Right of a property's `:`, before its terminating `;`/`}` — offers that
    // property's keyword values (docs/cosmos_core_spec.md §2), if any.
    Value,
    // Inside an unclosed `var(` — offers in-scope `--variable` names.
    VarRef,
};

struct CompletionContext {
    CompletionContextKind Kind{CompletionContextKind::Statement};
    int                   Depth{0};   // brace depth at the cursor; meaningful for Statement
    std::string           Property;  // populated when Kind == Value
};

// Tokenizes Source in full and classifies what a completion request at (Line, Column)
// (1-based, matching Cosmos::SourceLocation's convention) is completing. A pragmatic
// token-based classifier, not a real incremental parse — mirrors the role
// iris-lsp's own RenderTextHeuristics plays for render{} text, just built on Cosmos's
// token stream instead of raw text since Cosmos::Tokenizer is cheap enough to always
// re-run in full (§0 goal — see Server.h's own class comment).
CompletionContext ClassifyCompletionContext(std::string_view Source, std::uint32_t Line, std::uint32_t Column);

// Every token in Source, ending with the EndOfFile token.
std::vector<Cosmos::Token> TokenizeDocument(std::string_view Source);

// Returns the token whose source span contains (Line, Column), if any — used by
// goto-definition to find the `var(--name)` reference (or declaration) under the
// cursor.
std::optional<Cosmos::Token> TokenAtPosition(std::string_view Source, std::uint32_t Line, std::uint32_t Column);

// Every `--name` the resolver sees in Sheet: :root's, and every rule's at any depth, as
// Cosmos::Resolver's variable scope collects them.
std::vector<const Cosmos::VariableDeclaration*> CollectInScopeVariables(const Cosmos::Stylesheet& Sheet);

// :root's variables alone: all of global.cosmos a component's stylesheet can see.
std::vector<const Cosmos::VariableDeclaration*> CollectRootVariables(const Cosmos::Stylesheet& Sheet);

// Static language data, straight from docs/cosmos_core_spec.md §1.1/§2.
extern const std::vector<std::string_view> kPropertyNames;
extern const std::vector<std::string_view> kPrimitiveSelectorNames;
extern const std::vector<std::string_view> kPseudoClassNames;

// Keyword values a property accepts (§2's enum-shaped properties only — `display`,
// `flex-direction`, `align-items`, `text-overflow`); empty for every other property
// (colors/lengths/strings/var()-only values have no fixed keyword set to complete).
std::vector<std::string_view> PropertyValueKeywords(std::string_view Property);

} // namespace CosmosLsp
