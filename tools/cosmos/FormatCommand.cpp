#include "CommandSupport.h"
#include "CosmosCli.h"
#include "FormatSettings.h"

#include "Cosmos/Formatter.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace CosmosCli {

namespace {

std::optional<std::string> ReadText(const std::string& Path) {
    std::ifstream Stream(Path, std::ios::binary);
    if (!Stream) {
        return std::nullopt;
    }
    std::ostringstream Contents;
    Contents << Stream.rdbuf();
    return Contents.str();
}

bool WriteText(const std::string& Path, const std::string& Text) {
    std::ofstream Stream(Path, std::ios::binary | std::ios::trunc);
    if (!Stream) {
        return false;
    }
    Stream << Text;
    return static_cast<bool>(Stream);
}

} // namespace

int FormatCommand(const std::vector<std::string>& Arguments, std::ostream& Out, std::ostream& Err) {
    bool                     CheckOnly = false;
    std::vector<std::string> Files;
    for (const std::string& Argument : Arguments) {
        if (Argument == "--check") {
            CheckOnly = true;
        } else if (Argument.starts_with("-")) {
            return UsageError("'cosmos fmt' has no option '" + Argument + "'", Err);
        } else if (std::filesystem::is_directory(Argument)) {
            std::ranges::move(CosmosFilesUnder(Argument), std::back_inserter(Files));
        } else if (std::filesystem::path(Argument).extension() != ".cosmos") {
            Err << "note: skipping '" << Argument << "', which isn't a .cosmos file\n";
        } else {
            Files.push_back(Argument);
        }
    }
    if (Arguments.empty() || (Arguments.size() == 1 && CheckOnly)) {
        return UsageError("'cosmos fmt' takes at least one file or directory", Err);
    }

    std::set<std::string> Reported;
    std::size_t           Failures = 0;
    std::size_t           Changed  = 0;
    for (const std::string& Path : Files) {
        const std::optional<std::string> Text = ReadText(Path);
        if (!Text) {
            Err << "error: can't read '" << Path << "'\n";
            ++Failures;
            continue;
        }
        const CosmosTools::FormatSettings Settings = CosmosTools::FormatSettingsFor(Path);
        for (const std::string& Problem : Settings.Problems) {
            if (Reported.insert(Problem).second) {
                Err << "warning: " << Problem << "\n";
            }
        }
        const Cosmos::FormatResult Result = Cosmos::FormatSource(*Text, Path, Settings.Options);
        if (Result.Outcome == Cosmos::FormatOutcome::SyntaxErrors) {
            for (const Cosmos::ParseError& Error : Result.Errors) {
                Err << Error.Location.FilePath << ":" << Error.Location.Line << ":" << Error.Location.Column
                    << ": error: " << Error.Message << "\n";
            }
            Err << "error: " << Path << ": not formatted, because it has syntax errors\n";
            ++Failures;
            continue;
        }
        if (Result.Outcome == Cosmos::FormatOutcome::Unverified) {
            Err << "error: " << Path << ": not formatted, because " << Result.Problem
                << ". This is a formatter bug; the file is unchanged\n";
            ++Failures;
            continue;
        }
        if (Result.Formatted == *Text) {
            continue;
        }
        ++Changed;
        if (CheckOnly) {
            Out << Path << "\n";
            continue;
        }
        if (!WriteText(Path, Result.Formatted)) {
            Err << "error: can't write '" << Path << "'\n";
            ++Failures;
            continue;
        }
        Out << "formatted " << Path << "\n";
    }

    if (Failures > 0) {
        return kExitFailure;
    }
    if (CheckOnly && Changed > 0) {
        Err << Changed << (Changed == 1 ? " file isn't" : " files aren't") << " formatted\n";
        return kExitFailure;
    }
    return kExitSuccess;
}

} // namespace CosmosCli
