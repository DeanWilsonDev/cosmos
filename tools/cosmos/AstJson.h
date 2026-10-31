#pragma once

#include "cosmos/Ast.h"

#include <amanuensis.hpp>

namespace CosmosCli {

// The syntax tree as JSON, for `cosmos ast`. With Locations, each node also has a
// "location" (and, where the AST records one, an "end") as "line:column".
Amanuensis::JsonValue StylesheetToJson(const Cosmos::Stylesheet& Sheet, bool Locations);

} // namespace CosmosCli
