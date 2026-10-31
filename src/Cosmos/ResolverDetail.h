#pragma once

#include "cosmos/Ast.h"
#include "cosmos/Resolver.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Cosmos {

struct VariableScope {
    std::unordered_map<std::string, ValuePart> Values;
};

namespace Detail {

// Where a value leads once each var() is followed: the value it ends at, or the name no
// declaration has (and the value that names it), or the names that lead back to themselves.
struct VariableLookup {
    const ValuePart*         Value{nullptr};
    std::string              Missing;
    const ValuePart*         MissingAt{nullptr};
    std::vector<std::string> Cycle;
};

VariableLookup LookUpVariable(const ValuePart& Part, const VariableScope& Scope);
std::string    UndefinedVariableMessage(const std::string& Name);
std::string    VariableCycleMessage(const std::vector<std::string>& Cycle);

VariableScope BuildScope(const StylesheetSet& Sheets);

void ApplyOneDeclaration(const Declaration& Decl, const VariableScope& Scope, std::string_view TargetTag,
                         std::vector<ResolveDiagnostic>& Diagnostics);

std::string StartingStyleInPseudoMessage(PseudoKind Kind);

// Every rule in Sheet whose full selector matches Target, each as the chain of rules from
// the top-level one down to it, in the order the resolver applies them.
std::vector<std::vector<const Rule*>> MatchingRuleChains(const Stylesheet& Sheet, const IStyleTarget& Target,
                                                         bool Unbounded);

// Values as written: "8px 16px", "width 0.2s, height 0.4s", "var(--gap)".
std::string WrittenValues(const std::vector<ValuePart>& Values);

// Target's style from both layers, before anything is inherited from its ancestors.
ResolvedStyle ResolveOwnStyle(const IStyleTarget& Target, const StylesheetSet& Sheets,
                              std::vector<ResolveDiagnostic>& Diagnostics);

} // namespace Detail

} // namespace Cosmos
