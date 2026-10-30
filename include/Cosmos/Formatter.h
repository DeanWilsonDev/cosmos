#pragma once

#include "Cosmos/Parser.h"

#include <string>
#include <string_view>
#include <vector>

namespace Cosmos {

struct FormatOptions {
    int LineWidth{100};
    int IndentWidth{4};
};

enum class FormatOutcome {
    Formatted,
    // The source doesn't parse, so it's left alone; Errors says why.
    SyntaxErrors,
    // The output didn't re-parse to the same syntax tree and comments, so it's withheld;
    // Problem says how it differed. That's a formatter bug.
    Unverified,
};

struct FormatResult {
    FormatOutcome           Outcome{FormatOutcome::Formatted};
    std::string             Formatted;
    std::vector<ParseError> Errors;
    std::string             Problem;
};

// Lays out a stylesheet: one declaration per line, blocks indented by IndentWidth, a blank
// line between top-level blocks, at most one blank line kept wherever the author left
// some, and every comment kept where it was, at the end of its line or on a line of its own.
// A comma-separated value too long for LineWidth puts each group on its own line.
FormatResult FormatSource(std::string_view Source, const std::string& FilePath, const FormatOptions& Options = {});

} // namespace Cosmos
