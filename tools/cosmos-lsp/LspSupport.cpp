#include "LspSupport.h"

#include <fstream>
#include <sstream>

namespace CosmosLsp {

std::string UriToPath(const std::string& Uri) {
    constexpr std::string_view Prefix = "file://";
    return Uri.substr(0, Prefix.size()) == Prefix ? Uri.substr(Prefix.size()) : Uri;
}

std::string PathToUri(const std::string& Path) { return "file://" + Path; }

std::optional<std::string> ReadFile(const std::filesystem::path& Path) {
    std::ifstream Stream(Path, std::ios::binary);
    if (!Stream) {
        return std::nullopt;
    }
    std::ostringstream Buffer;
    Buffer << Stream.rdbuf();
    return Buffer.str();
}

Amanuensis::JsonValue MakePosition(std::uint32_t Line, std::uint32_t Column) {
    Amanuensis::JsonValue Position = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Position, "line", Amanuensis::JsonValue(static_cast<long long>(Line - 1)));
    Amanuensis::Json::Insert(Position, "character", Amanuensis::JsonValue(static_cast<long long>(Column - 1)));
    return Position;
}

Amanuensis::JsonValue MakeRange(std::uint32_t Line, std::uint32_t Column, std::uint32_t EndLine,
                                std::uint32_t EndColumn) {
    Amanuensis::JsonValue Range = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Range, "start", MakePosition(Line, Column));
    Amanuensis::Json::Insert(Range, "end", MakePosition(EndLine, EndColumn));
    return Range;
}

Amanuensis::JsonValue MakeRange(std::uint32_t Line, std::uint32_t Column) { return MakeRange(Line, Column, Line, Column); }

const Amanuensis::JsonValue* FindPath(const Amanuensis::JsonValue& Root, std::initializer_list<std::string_view> Keys) {
    const Amanuensis::JsonValue* Current = &Root;
    for (const std::string_view Key : Keys) {
        if (!Amanuensis::Json::IsObject(*Current) || !Amanuensis::Json::Contains(*Current, std::string(Key))) {
            return nullptr;
        }
        Current = &Amanuensis::Json::Get(*Current, std::string(Key));
    }
    return Current;
}

bool IsGlobalPath(const std::string& Path) { return std::filesystem::path(Path).filename() == "global.cosmos"; }

bool IsCosmosPath(const std::string& Path) { return std::filesystem::path(Path).extension() == ".cosmos"; }

Amanuensis::JsonValue MakeDiagnostic(Amanuensis::JsonValue Range, long long Severity, const std::string& Message) {
    Amanuensis::JsonValue Diagnostic = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Diagnostic, "range", std::move(Range));
    Amanuensis::Json::Insert(Diagnostic, "severity", Amanuensis::JsonValue(Severity));
    Amanuensis::Json::Insert(Diagnostic, "source", Amanuensis::JsonValue("cosmos"));
    Amanuensis::Json::Insert(Diagnostic, "message", Amanuensis::JsonValue(Message));
    return Diagnostic;
}

} // namespace CosmosLsp
