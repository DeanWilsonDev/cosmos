#pragma once

#include "cosmos/Formatter.h"

#include <filesystem>
#include <string>
#include <vector>

namespace CosmosTools {

struct FormatSettings {
    Cosmos::FormatOptions    Options;
    // What was wrong with the project file, if anything; the defaults stand in for it.
    std::vector<std::string> Problems;
};

// The formatter's settings for File: the "format" object of the nearest .nyx.json above it,
// the project file nyx fmt and chaos fmt read, so one file sets all three. Without one, the
// defaults (a line width of 100 and an indent of 4).
FormatSettings FormatSettingsFor(const std::filesystem::path& File);

} // namespace CosmosTools
