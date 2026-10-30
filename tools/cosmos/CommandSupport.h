#pragma once

#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

namespace CosmosCli {

int UsageError(const std::string& Message, std::ostream& Err);

// Every .cosmos file below Folder, sorted, skipping hidden, build, build-*, build_*,
// node_modules and _deps folders.
std::vector<std::string> CosmosFilesUnder(const std::filesystem::path& Folder);

int FormatCommand(const std::vector<std::string>& Arguments, std::ostream& Out, std::ostream& Err);

} // namespace CosmosCli
