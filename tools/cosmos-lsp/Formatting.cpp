#include "Server.h"

#include "FormatSettings.h"
#include "LspSupport.h"

#include "cosmos/Formatter.h"

#include <algorithm>

namespace CosmosLsp {

namespace {

using Json = Amanuensis::Json;
using Amanuensis::JsonValue;

constexpr long long kRequestFailed = -32803;

// Lines of Text with their newlines, so a missing final newline is a difference too.
std::vector<std::string_view> LinesOf(std::string_view Text) {
    std::vector<std::string_view> Lines;
    std::size_t                   Start = 0;
    while (Start < Text.size()) {
        const std::size_t Newline = Text.find('\n', Start);
        const std::size_t End     = Newline == std::string_view::npos ? Text.size() : Newline + 1;
        Lines.push_back(Text.substr(Start, End - Start));
        Start = End;
    }
    return Lines;
}

JsonValue PositionAtOffset(std::string_view Text, std::size_t Offset) {
    std::uint32_t Line   = 1;
    std::uint32_t Column = 1;
    for (std::size_t I = 0; I < Offset && I < Text.size(); ++I) {
        if (Text[I] == '\n') {
            ++Line;
            Column = 1;
        } else {
            ++Column;
        }
    }
    return MakePosition(Line, Column);
}

} // namespace

std::vector<LineReplacement> LineEditsBetween(std::string_view Original, std::string_view Formatted) {
    const std::vector<std::string_view> From = LinesOf(Original);
    const std::vector<std::string_view> To   = LinesOf(Formatted);
    const std::size_t                   N    = From.size();
    const std::size_t                   M    = To.size();
    std::vector<std::vector<std::uint32_t>> Common(N + 1, std::vector<std::uint32_t>(M + 1, 0));
    for (std::size_t I = N; I-- > 0;) {
        for (std::size_t J = M; J-- > 0;) {
            Common[I][J] = From[I] == To[J] ? Common[I + 1][J + 1] + 1 : std::max(Common[I + 1][J], Common[I][J + 1]);
        }
    }

    std::vector<std::size_t> FromOffsets(N + 1, 0);
    for (std::size_t I = 0; I < N; ++I) {
        FromOffsets[I + 1] = FromOffsets[I] + From[I].size();
    }

    std::vector<LineReplacement> Edits;
    std::size_t                  I = 0;
    std::size_t                  J = 0;
    while (I < N || J < M) {
        if (I < N && J < M && From[I] == To[J]) {
            ++I;
            ++J;
            continue;
        }
        LineReplacement Edit{FromOffsets[I], FromOffsets[I], ""};
        while ((I < N || J < M) && !(I < N && J < M && From[I] == To[J])) {
            if (J < M && (I == N || Common[I][J + 1] >= Common[I + 1][J])) {
                Edit.Text += To[J++];
            } else {
                ++I;
            }
        }
        Edit.EndOffset = FromOffsets[I];
        Edits.push_back(std::move(Edit));
    }
    return Edits;
}

void Server::HandleFormatting(const JsonValue& Id, const JsonValue& Params, bool OnlyRange) {
    const std::string Uri = Json::AsString(Json::Get(Json::Get(Params, "textDocument"), "uri"));
    const auto        Doc = Documents_.find(Uri);
    if (Doc == Documents_.end()) {
        ReplyError(Id, -32602, "the document isn't open");
        return;
    }
    const std::string&                   Text = Doc->second.Text;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> Lines;
    if (OnlyRange) {
        const JsonValue* Start = FindPath(Params, {"range", "start", "line"});
        const JsonValue* End   = FindPath(Params, {"range", "end", "line"});
        if (!Start || !End || !Json::IsInteger(*Start) || !Json::IsInteger(*End)) {
            ReplyError(Id, -32602, "the request has no range");
            return;
        }
        Lines = std::pair{static_cast<std::uint32_t>(Json::AsInteger(*Start)),
                          static_cast<std::uint32_t>(Json::AsInteger(*End))};
    }

    const std::string                 Path     = UriToPath(Uri);
    const Cosmos::FormatResult        Result   = Cosmos::FormatSource(Text, Path, CosmosTools::FormatSettingsFor(Path).Options);
    if (Result.Outcome == Cosmos::FormatOutcome::Unverified) {
        ReplyError(Id, kRequestFailed, "not formatted, because " + Result.Problem + ". This is a formatter bug");
        return;
    }
    JsonValue Edits = Json::MakeArray();
    if (Result.Outcome == Cosmos::FormatOutcome::Formatted) {
        for (const LineReplacement& Replacement : LineEditsBetween(Text, Result.Formatted)) {
            JsonValue       Start = PositionAtOffset(Text, Replacement.StartOffset);
            JsonValue       End   = PositionAtOffset(Text, Replacement.EndOffset);
            const long long First = Json::AsInteger(Json::Get(Start, "line"));
            const long long Last  = Json::AsInteger(Json::Get(End, "line"));
            if (Lines && (First > Lines->second || Last < Lines->first)) {
                continue;
            }
            JsonValue Range = Json::MakeObject();
            Json::Insert(Range, "start", std::move(Start));
            Json::Insert(Range, "end", std::move(End));
            JsonValue Edit = Json::MakeObject();
            Json::Insert(Edit, "range", std::move(Range));
            Json::Insert(Edit, "newText", JsonValue(Replacement.Text));
            Json::PushBack(Edits, std::move(Edit));
        }
    }
    Reply(Id, std::move(Edits));
}

} // namespace CosmosLsp
