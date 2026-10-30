#pragma once

#include "Cosmos/Ast.h"
#include "Cosmos/ResolvedStyle.h"

#include <string>
#include <string_view>
#include <vector>

namespace Cosmos {

// Backend/host-agnostic view onto one element of whatever real render tree
// the host owns (an Iris `IrisComponent`/mounted-widget tree, in practice).
// A host adapter implements this over its own node type; the resolver never
// depends on Iris or any backend directly (docs/cosmos_core_spec.md §0 goal
// 4). Mirrors the abstraction-point pattern `iris`'s own
// IHostLanguageTokenizer already uses for the same reason.
class IStyleTarget {
public:
    virtual ~IStyleTarget() = default;

    // The element's `class` prop value; empty if none was set.
    virtual std::string ClassName() const = 0;

    // The element's real Iris tag, PascalCase ("Frame", "Inline", "Grid",
    // "Image", "Text") -- compared against §1.1's mapping table.
    virtual std::string PrimitiveTag() const = 0;

    // True exactly for the root element of the component instance currently
    // being styled. Descendant-selector ancestor walks (§1.2) stop climbing
    // once they reach a node where this is true -- a component's own
    // stylesheet can select down to and including its own root, never past
    // it. Callers resolving `global.cosmos` (which has no "own subtree" to
    // be bounded by, §1.2) should pass Unbounded=true to Resolve() below,
    // which makes the walk ignore this entirely.
    virtual bool IsComponentRoot() const = 0;

    virtual const IStyleTarget* Parent() const = 0;
};

// The two-layer cascade (§1.3): a stylesheet parsed from global.cosmos, and
// the component's own paired Name.cosmos. Either may be null (a project
// might have no global.cosmos yet; not every component needs its own
// styles).
struct StylesheetSet {
    const Stylesheet* Global{nullptr};
    const Stylesheet* Component{nullptr};
};

struct ResolveDiagnostic {
    std::string Message;
    // The declaration, value or rule the diagnostic is about. Its FilePath is the path the
    // stylesheet was parsed with.
    SourceLocation Location{"", 0, 0};
    SourceLocation End{"", 0, 0};
    // Text that fixes the diagnostic when it replaces Location..End, if there's one likely fix.
    std::string Suggestion;
};

// Parses, cascades, and resolves variables/selectors down to a concrete
// ResolvedStyle for one element. Stateless beyond its inputs -- safe to
// reuse across many Resolve() calls.
class Resolver {
public:
    // Unbounded should be true only when Sheets.Component *is* global.cosmos
    // itself (i.e. Target's whole subtree is being matched against
    // global.cosmos, which has no component-boundary restriction, §1.2).
    // For an ordinary component's own stylesheet, leave it false so
    // ancestor walks stop at Target's own component root.
    ResolvedStyle Resolve(const IStyleTarget& Target, const StylesheetSet& Sheets, bool Unbounded,
                           std::vector<ResolveDiagnostic>& OutDiagnostics) const;
};

// §1.1's closed mapping table, exposed for callers that need it directly
// (e.g. a host adapter building an IStyleTarget). Returns "" for a name
// outside the five Core primitives.
std::string_view PrimitiveTagForSelector(std::string_view CosmosSelectorName);

// §1.7: resolves Target against both cascade layers (§1.3) into one merged ResolvedStyle --
// global.cosmos Unbounded, the component's own file bounded to Target's own component root,
// composed correctly in one call (Resolver::Resolve() alone can't express differing
// Unbounded per layer, see §1.7's own note) -- then fills any still-unset `color`,
// `font-family`, `font-size`, `font-style` or `pointer-events` from the nearest ancestor
// (walking Target.Parent()) that sets it. This is CSS's own inheritance model, deliberately
// scoped to exactly those properties; every other field stays non-inherited, matching real
// CSS. Crosses component boundaries (unlike §1.2's
// selector-boundary rule -- inheritance is value propagation down an already-matched tree,
// not selector matching, so it isn't bounded by IsComponentRoot()). Pseudo-class overlays
// (Hover/Active/Disabled) never participate: they're resolved once, statically, per
// element, and Cosmos has no live per-frame recomputation of a widget's actual interaction
// state to make a parent's :hover value flow dynamically into a child's the way real CSS
// would.
ResolvedStyle ResolveStyle(const IStyleTarget& Target, const StylesheetSet& Sheets,
                            std::vector<ResolveDiagnostic>& OutDiagnostics);

} // namespace Cosmos
