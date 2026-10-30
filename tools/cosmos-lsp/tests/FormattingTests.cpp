#include "cimmerian/test.hpp"

#include "ServerHarness.h"

using namespace CosmosLsp;
using namespace CosmosLspTests;

namespace {

using Amanuensis::Json;
using Amanuensis::JsonValue;

std::string Applied(std::string Text, const std::vector<LineReplacement>& Edits) {
    for (auto It = Edits.rbegin(); It != Edits.rend(); ++It) {
        Text.replace(It->StartOffset, It->EndOffset - It->StartOffset, It->Text);
    }
    return Text;
}

std::string Applied(std::string Text, const JsonValue& Edits) {
    std::vector<std::size_t> LineStarts{0};
    for (std::size_t I = 0; I < Text.size(); ++I) {
        if (Text[I] == '\n') {
            LineStarts.push_back(I + 1);
        }
    }
    auto Offset = [&](const JsonValue& Position) {
        const auto Line = static_cast<std::size_t>(IntegerAt(Position, {"line"}));
        return (Line < LineStarts.size() ? LineStarts[Line] : Text.size()) +
               static_cast<std::size_t>(IntegerAt(Position, {"character"}));
    };
    std::vector<LineReplacement> Replacements;
    for (std::size_t I = 0; I < Json::Size(Edits); ++I) {
        const JsonValue& Edit = Json::At(Edits, I);
        Replacements.push_back(LineReplacement{Offset(Json::Get(Json::Get(Edit, "range"), "start")),
                                               Offset(Json::Get(Json::Get(Edit, "range"), "end")),
                                               Json::AsString(Json::Get(Edit, "newText"))});
    }
    return Applied(std::move(Text), Replacements);
}

JsonValue MakeRangeFormatting(int Id, const std::string& Uri, long long FirstLine, long long LastLine) {
    JsonValue Request = MakeRequest(Id, "textDocument/rangeFormatting", Uri, 0, 0);
    auto      At      = [](long long Line) {
        JsonValue Position = Json::MakeObject();
        Json::Insert(Position, "line", JsonValue(Line));
        Json::Insert(Position, "character", JsonValue(0LL));
        return Position;
    };
    JsonValue Range = Json::MakeObject();
    Json::Insert(Range, "start", At(FirstLine));
    Json::Insert(Range, "end", At(LastLine));
    return WithParam(Request, "range", Range);
}

const std::string kMessy = ".a{gap: 1px;}\n\n.b{gap: 2px;}\n";

} // namespace

DESCRIBE("Server formatting", {
    IT("turns one text into another with whole-line edits", {
        const std::string From = "keep\nold one\nold two\nkeep too\nno newline";
        const std::string To   = "keep\nnew\nkeep too\nno newline\n";
        ASSERT_EQUAL(Applied(From, LineEditsBetween(From, To)), To);
        ASSERT_TRUE(LineEditsBetween(To, To).empty());
    });

    IT("advertises formatting and range formatting", {
        const auto Responses = RunServer({MakeRequest(1, "initialize", "", 0, 0)});
        const JsonValue& Capabilities = Json::Get(Json::Get(Responses[0], "result"), "capabilities");
        ASSERT_TRUE(Json::AsBoolean(Json::Get(Capabilities, "documentFormattingProvider")));
        ASSERT_TRUE(Json::AsBoolean(Json::Get(Capabilities, "documentRangeFormattingProvider")));
    });

    IT("formats the whole document, or only the lines in a range", {
        ScratchFolder     Folder;
        const std::string Uri       = Folder.Uri("Messy.cosmos");
        const auto        Responses = RunServer({MakeDidOpen(Uri, kMessy),
                                                 MakeRequest(2, "textDocument/formatting", Uri, 0, 0),
                                                 MakeRangeFormatting(3, Uri, 2, 2)});
        ASSERT_EQUAL(Applied(kMessy, ResultOf(Responses, 2)),
                     std::string(".a {\n    gap: 1px;\n}\n\n.b {\n    gap: 2px;\n}\n"));
        ASSERT_EQUAL(Applied(kMessy, ResultOf(Responses, 3)),
                     std::string(".a{gap: 1px;}\n\n.b {\n    gap: 2px;\n}\n"));
    });

    IT("returns no edits for a document that doesn't parse", {
        ScratchFolder     Folder;
        const std::string Uri       = Folder.Uri("Broken.cosmos");
        const auto        Responses = RunServer({MakeDidOpen(Uri, ".a { gap: 1px"),
                                                 MakeRequest(2, "textDocument/formatting", Uri, 0, 0)});
        ASSERT_EQUAL(Json::Size(ResultOf(Responses, 2)), static_cast<std::size_t>(0));
    });
});
