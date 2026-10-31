#pragma once

#include "cosmos/Ast.h"
#include "cosmos/ResolvedStyle.h"
#include "cosmos/Resolver.h"

#include <string>
#include <vector>

namespace Cosmos {

// Which part of a ResolvedStyle a declaration writes: the element's own style, one of its
// pseudo-class overlays, or the @starting-style it appears from.
enum class StyleState { Base, Hover, Active, Disabled, Starting };

struct ExplainedDeclaration {
    const Declaration* Decl{nullptr};
    StyleState         State{StyleState::Base};
    // The declaration as written, without its semicolon: "color: var(--accent)".
    std::string Written;
    // The values with every var() followed, when the declaration has one; empty otherwise.
    std::string Resolved;
    // Why the resolver drops the declaration, when it does.
    std::string Problem;
    // A later declaration, in the cascade, that sets everything this one sets in the same
    // state.
    const Declaration* OverriddenBy{nullptr};
};

struct ExplainedRule {
    // The rules from the top-level one down to the one whose selector matched.
    std::vector<const Rule*>          Lineage;
    bool                              FromGlobal{false};
    std::vector<ExplainedDeclaration> Declarations;
};

struct StyleProperty {
    std::string Name;
    std::string Value;
};

// Why an element gets the style it does: every rule that matches it in the order the
// resolver applies them (global.cosmos's first, then the component's, each by depth and
// source order), what each declaration contributes, and the style that results.
struct StyleExplanation {
    std::vector<ExplainedRule>     Rules;
    ResolvedStyle                  Style;
    // Properties the element takes from an ancestor rather than from its own rules.
    std::vector<std::string>       InheritedProperties;
    std::vector<ResolveDiagnostic> Diagnostics;
};

StyleExplanation ExplainStyle(const IStyleTarget& Target, const StylesheetSet& Sheets);

// A rule's full selector, outermost first: ".card text:hover".
std::string SelectorChainText(const std::vector<const Rule*>& Lineage);

// Every property Style sets, in the property table's order, as Cosmos writes its value.
// A property set by attr() reads "attr(name)". Overlays aren't included.
std::vector<StyleProperty> DescribeStyle(const ResolvedStyle& Style);

} // namespace Cosmos
