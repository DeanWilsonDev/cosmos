#pragma once

#include "Lustre/SourceLocation.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Lustre {

// docs/lustre_core_spec.md §1.1 — a selector is a class selector or a
// primitive-element selector, never combined into a tag+class compound
// (`Frame.card { }` is a parse error). A pseudo-class may be suffixed
// directly onto either (`.card:hover { }`, §1.2's worked example) — that's
// the one exception to "never compound," and is represented on Rule as an
// attached PseudoKind rather than as a third SelectorKind, since a pseudo
// suffix never stands alone.
enum class SelectorKind {
    Class,     // .card
    Primitive, // frame, inline, grid, image, text
};

enum class PseudoKind { Hover, Active, Disabled };

// A raw property value as written in source. The parser does not interpret
// units, colors, or var() references any further than this — that's the
// resolver's job (§1.4's variable resolution, §1.5's unit resolution).
struct ValuePart {
    // Exactly one of these is populated, matching how the tokenizer already
    // classified the underlying token.
    std::optional<std::string> Literal;      // identifier/keyword value (e.g. "row", "stack")
    std::optional<std::string> Number;       // "16px", "0.2s", "100%", "1.5" — unit suffix included
    std::optional<std::string> StringValue;  // "assets/fonts/body.ttf"
    std::optional<std::string> HexColor;     // "#E8593C"
    std::optional<std::string> VariableRef;  // var(--name) -- the "name" part, no fallback (§1.4)
    std::optional<std::string> AttrRef;      // attr(name) -- the element's own `name` prop, read by the backend
    // Populated alongside Literal when a bare identifier is immediately
    // followed by a parenthesized argument -- v1's only case is
    // `transform: scale(1.5)` (§2), so this holds "1.5" when Literal holds
    // "scale". Not a general function-call grammar.
    std::optional<std::string> CallArgument;

    // True when a comma came just before this value: `transition: width 0.2s,
    // height 0.4s` is two groups, and the second's `height` follows a comma.
    bool FollowsComma{false};
};

struct Declaration {
    std::string            Property;
    std::vector<ValuePart> Values; // e.g. padding: 8px 16px -> two Values
    SourceLocation         Location;
};

struct VariableDeclaration {
    std::string    Name; // without the leading --
    ValuePart      Value;
    SourceLocation Location;
};

struct Rule;
using RulePtr = std::unique_ptr<Rule>;

// A single selector + its block. Nested rules (descendant selectors and
// pseudo-class blocks, §1.2) live in NestedRules. A rule's own selector is
// resolved relative to whatever rule (if any) contains it — a top-level rule
// has no ancestor selector.
struct Rule {
    SelectorKind Kind{SelectorKind::Class};

    // Populated when Kind == Class.
    std::string ClassName;
    // Populated when Kind == Primitive (already the lowercase Lustre form,
    // e.g. "frame" -- §1.1's mapping table is applied at resolve time).
    std::string PrimitiveName;
    // A pseudo-class suffixed directly onto this selector (`.card:hover`),
    // if any — see the SelectorKind comment above for why this isn't a
    // separate selector kind.
    std::optional<PseudoKind> Pseudo;

    std::vector<Declaration>         Declarations;
    std::vector<VariableDeclaration> Variables; // component-scoped --vars declared directly in this block (§1.4)
    std::vector<RulePtr>             NestedRules;

    // Declarations from an `@starting-style { }` block nested in this rule's own body,
    // as CSS nesting writes it: the values this rule's elements start from when they
    // appear, so their transitions ease them in. They apply to the same element as the
    // rule itself, not to a descendant the way a nested rule does.
    std::vector<Declaration> StartingDeclarations;

    // Nesting depth from the top level (0 for a top-level rule), and a
    // whole-file monotonic counter assigned at parse time. Together these
    // give the resolver §1.2's specificity rule (deeper nesting wins) with
    // source order as the tiebreaker, without needing parent pointers.
    int         Depth{0};
    std::size_t SourceIndex{0};

    SourceLocation Location;
};

// A `:root { }` block — declares global.lustre's project-wide variables
// (§1.4). Only valid at the top level of global.lustre; the parser doesn't
// enforce *which file* it appears in (that's a whole-project concern outside
// a single-file parse), only that it's well-formed.
struct RootBlock {
    std::vector<VariableDeclaration> Variables;
    SourceLocation                   Location;
};

struct RuleIndex;
struct CachedVariableScope;

struct Stylesheet {
    std::optional<RootBlock> Root;
    std::vector<RulePtr>     Rules;

    mutable std::shared_ptr<const RuleIndex>           MatchIndex;
    mutable std::shared_ptr<const CachedVariableScope> VariableScopeCache;
};

} // namespace Lustre
