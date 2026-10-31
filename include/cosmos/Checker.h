#pragma once

#include "cosmos/Resolver.h"
#include "cosmos/SourceLocation.h"

#include <string>
#include <vector>

namespace Cosmos {

enum class CheckSeverity { Error, Warning };

struct CheckDiagnostic {
    CheckSeverity  Severity{CheckSeverity::Error};
    std::string    Message;
    SourceLocation Location;
    SourceLocation End;
    // Text that fixes the diagnostic when it replaces Location..End, if there's one likely fix.
    std::string Suggestion;
};

// Checks one stylesheet without a render tree: Sheets.Component, or Sheets.Global when
// there's no component sheet. Reports unknown properties (warnings), undefined variables,
// values the resolver would drop, container-only properties under a primitive selector for a
// leaf, and `@starting-style` inside a pseudo-class rule. A component sheet checked without a
// global sheet skips undefined variables, since they may be declared in a global.cosmos the
// caller didn't find.
std::vector<CheckDiagnostic> CheckStylesheet(const StylesheetSet& Sheets);

} // namespace Cosmos
