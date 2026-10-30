#pragma once

#include "Cosmos/Ast.h"
#include "Cosmos/Resolver.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Cosmos {

struct VariableScope {
    std::unordered_map<std::string, ValuePart> Values;
};

namespace Detail {

VariableScope BuildScope(const StylesheetSet& Sheets);

void ApplyOneDeclaration(const Declaration& Decl, const VariableScope& Scope, std::string_view TargetTag,
                         std::vector<ResolveDiagnostic>& Diagnostics);

std::string StartingStyleInPseudoMessage(PseudoKind Kind);

} // namespace Detail

} // namespace Cosmos
