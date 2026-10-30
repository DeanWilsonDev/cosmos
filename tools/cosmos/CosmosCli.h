#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace CosmosCli {

inline constexpr int kExitSuccess = 0;
inline constexpr int kExitFailure = 1;
inline constexpr int kExitUsage   = 2;

// Runs `cosmos <command> [arguments]` with Arguments starting at the command, writing
// results to Out and diagnostics to Err; returns the exit code.
int RunCosmosCli(const std::vector<std::string>& Arguments, std::ostream& Out, std::ostream& Err);

} // namespace CosmosCli
