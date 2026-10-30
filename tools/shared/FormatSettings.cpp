#include "FormatSettings.h"

#include <amanuensis.hpp>

#include <fstream>
#include <sstream>

namespace CosmosTools {

namespace {

using Json = Amanuensis::Json;

void ReadWidth(const Amanuensis::JsonValue& Format, const std::string& Key, long long Least, long long Most,
               int& Into, std::vector<std::string>& Problems) {
    const Amanuensis::JsonValue* Value = Json::Find(Format, Key);
    if (!Value) {
        return;
    }
    if (!Json::IsInteger(*Value) || Json::AsInteger(*Value) < Least || Json::AsInteger(*Value) > Most) {
        Problems.push_back("'format." + Key + "' must be a whole number from " + std::to_string(Least) + " to " +
                           std::to_string(Most));
        return;
    }
    Into = static_cast<int>(Json::AsInteger(*Value));
}

} // namespace

FormatSettings FormatSettingsFor(const std::filesystem::path& File) {
    FormatSettings  Settings;
    std::error_code Error;
    std::filesystem::path Folder = std::filesystem::absolute(File, Error).parent_path();
    while (!Folder.empty()) {
        const std::filesystem::path ProjectFile = Folder / ".nyx.json";
        if (std::filesystem::is_regular_file(ProjectFile, Error)) {
            std::ifstream      Stream(ProjectFile, std::ios::binary);
            std::ostringstream Contents;
            Contents << Stream.rdbuf();
            const Amanuensis::JsonParseResult Parsed = Amanuensis::Reader::ParseString(Contents.str());
            if (!Parsed.succeeded || !Json::IsObject(Parsed.value)) {
                Settings.Problems.push_back(ProjectFile.string() + " isn't a JSON object");
                return Settings;
            }
            const Amanuensis::JsonValue* Format = Json::Find(Parsed.value, "format");
            if (!Format) {
                return Settings;
            }
            if (!Json::IsObject(*Format)) {
                Settings.Problems.push_back("'format' in " + ProjectFile.string() +
                                            " must be an object, such as { \"lineWidth\": 100, \"indentWidth\": 4 }");
                return Settings;
            }
            ReadWidth(*Format, "lineWidth", 20, 400, Settings.Options.LineWidth, Settings.Problems);
            ReadWidth(*Format, "indentWidth", 1, 16, Settings.Options.IndentWidth, Settings.Problems);
            return Settings;
        }
        if (Folder == Folder.root_path()) {
            break;
        }
        Folder = Folder.parent_path();
    }
    return Settings;
}

} // namespace CosmosTools
