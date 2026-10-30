#include "cimmerian/test.hpp"

#include "LspSupport.h"
#include "ServerHarness.h"

#include <algorithm>
#include <cmath>

using namespace CosmosLsp;
using namespace CosmosLspTests;

namespace {

using Amanuensis::Json;
using Amanuensis::JsonValue;

const std::string kGlobal = ":root {\n"
                            "    --brand: #4a90ff;\n"
                            "    --accent: var(--brand);\n"
                            "}\n"
                            ".card { border-width: 1px; }\n";

const std::string kCard = ".card {\n"
                          "    --inset: 8px;\n"
                          "    background-color: var(--accent);\n"
                          "    padding: var(--inset);\n"
                          "    text:hover { color: var(--accent); }\n"
                          "}\n"
                          ".card:hover { opacity: 0.5; }\n";

const std::string kChaos = "Card :: (props: CardProps) -> Penumbra:::Component {\n"
                           "    return <Frame class=\"card wide\"><Text class=\"title\">{props.title}</Text></Frame>;\n"
                           "}\n";

const std::string kFix = ".fix {\n"
                         "    colr: #ffffff;\n"
                         "    display: stak;\n"
                         "    color: var(--accnt);\n"
                         "}\n";

struct Project {
    ScratchFolder Folder;

    Project() {
        Folder.Write("global.cosmos", kGlobal);
        Folder.Write("Card.cosmos", kCard);
        Folder.Write("Other.cosmos", ".other { color: var(--accent); }\n");
        Folder.Write("Shadow.cosmos", ":root { --accent: #000000; }\n.shadow { color: var(--accent); }\n");
        Folder.Write("Card.chaos", kChaos);
        Folder.Write("Commented.chaos", "// <Frame class=\"card\"/>\n");
        Folder.Write("Fix.cosmos", kFix);
    }

    std::vector<JsonValue> Run(std::vector<JsonValue> Requests) const {
        Requests.insert(Requests.begin(), MakeDidOpen(Folder.Uri("Card.cosmos"), kCard));
        return RunServer(Requests);
    }
};

JsonValue MakeRename(int Id, const std::string& Uri, std::uint32_t Line0, std::uint32_t Character0,
                     const std::string& NewName) {
    return WithParam(MakeRequest(Id, "textDocument/rename", Uri, Line0, Character0), "newName", JsonValue(NewName));
}

JsonValue MakeRange0(long long Line0, long long Character0, long long EndLine0, long long EndCharacter0) {
    auto Position = [](long long Line, long long Character) {
        JsonValue At = Json::MakeObject();
        Json::Insert(At, "line", JsonValue(Line));
        Json::Insert(At, "character", JsonValue(Character));
        return At;
    };
    JsonValue Range = Json::MakeObject();
    Json::Insert(Range, "start", Position(Line0, Character0));
    Json::Insert(Range, "end", Position(EndLine0, EndCharacter0));
    return Range;
}

std::string Where(const std::string& File, const JsonValue& Range) {
    return File + ":" + std::to_string(IntegerAt(Range, {"start", "line"}) + 1) + ":" +
           std::to_string(IntegerAt(Range, {"start", "character"}) + 1);
}

// Each edit in a WorkspaceEdit's changes as "File:line:column=newText", sorted.
std::vector<std::string> EditsOf(const JsonValue& WorkspaceEdit) {
    std::vector<std::string> Out;
    const JsonValue&         Changes = Json::Get(WorkspaceEdit, "changes");
    for (auto It = Json::BeginObject(Changes); It != Json::EndObject(Changes); ++It) {
        const std::string File = std::filesystem::path(It->first).filename().string();
        for (std::size_t I = 0; I < Json::Size(It->second); ++I) {
            const JsonValue& Edit = Json::At(It->second, I);
            Out.push_back(Where(File, Json::Get(Edit, "range")) + "=" + Json::AsString(Json::Get(Edit, "newText")));
        }
    }
    std::ranges::sort(Out);
    return Out;
}

double NumberAt(const JsonValue& Value, std::initializer_list<std::string_view> Keys) {
    const JsonValue* Found = FindPath(Value, Keys);
    return Json::IsInteger(*Found) ? static_cast<double>(Json::AsInteger(*Found)) : Json::AsDouble(*Found);
}

std::vector<std::string> LabelsOf(const JsonValue& Presentations) {
    std::vector<std::string> Labels;
    for (std::size_t I = 0; I < Json::Size(Presentations); ++I) {
        Labels.push_back(Json::AsString(Json::Get(Json::At(Presentations, I), "label")));
    }
    return Labels;
}

JsonValue MakeColorRequest(int Id, const std::string& Uri, double Red, double Green, double Blue, double Alpha) {
    JsonValue Color = Json::MakeObject();
    Json::Insert(Color, "red", JsonValue(Red));
    Json::Insert(Color, "green", JsonValue(Green));
    Json::Insert(Color, "blue", JsonValue(Blue));
    Json::Insert(Color, "alpha", JsonValue(Alpha));
    JsonValue Request = WithParam(MakeRequest(Id, "textDocument/colorPresentation", Uri, 0, 0), "color", Color);
    return WithParam(Request, "range", MakeRange0(0, 0, 0, 7));
}

} // namespace

DESCRIBE("Server editing", {
    IT("advertises rename with prepare, quick fixes, colours and folding", {
        const auto Responses = RunServer({MakeRequest(1, "initialize", "", 0, 0)});
        const JsonValue& Capabilities = Json::Get(Json::Get(Responses[0], "result"), "capabilities");
        ASSERT_TRUE(Json::AsBoolean(Json::Get(Json::Get(Capabilities, "renameProvider"), "prepareProvider")));
        ASSERT_EQUAL(Json::AsString(Json::At(Json::Get(Json::Get(Capabilities, "codeActionProvider"), "codeActionKinds"), 0)),
                     std::string("quickfix"));
        ASSERT_TRUE(Json::AsBoolean(Json::Get(Capabilities, "colorProvider")));
        ASSERT_TRUE(Json::AsBoolean(Json::Get(Capabilities, "foldingRangeProvider")));
    });

    IT("prepares a rename over the name as written, and refuses a property", {
        Project    P;
        const auto Responses =
            P.Run({MakeRequest(2, "textDocument/prepareRename", P.Folder.Uri("Card.cosmos"), 2, 30),
                   MakeRequest(3, "textDocument/prepareRename", P.Folder.Uri("Card.chaos"), 1, 27),
                   MakeRequest(4, "textDocument/prepareRename", P.Folder.Uri("Card.cosmos"), 3, 6)});
        const JsonValue Variable = ResultOf(Responses, 2);
        ASSERT_EQUAL(Json::AsString(Json::Get(Variable, "placeholder")), std::string("--accent"));
        ASSERT_EQUAL(Where("Card.cosmos", Json::Get(Variable, "range")), std::string("Card.cosmos:3:27"));
        ASSERT_EQUAL(IntegerAt(Variable, {"range", "end", "character"}), 34LL);
        ASSERT_EQUAL(Json::AsString(Json::Get(ResultOf(Responses, 3), "placeholder")), std::string("card"));
        ASSERT_TRUE(Json::IsNull(ResultOf(Responses, 4)));
    });

    IT("renames a global variable everywhere it's used, but not where a file shadows it", {
        Project    P;
        const auto Expected = (std::vector<std::string>{"Card.cosmos:3:27=--tone", "Card.cosmos:5:29=--tone",
                                                        "Other.cosmos:1:21=--tone", "global.cosmos:3:5=--tone"});
        ASSERT_EQUAL(EditsOf(ResultOf(P.Run({MakeRename(2, P.Folder.Uri("Card.cosmos"), 2, 30, "--tone")}), 2)),
                     Expected);
        ASSERT_EQUAL(EditsOf(ResultOf(P.Run({MakeRename(2, P.Folder.Uri("global.cosmos"), 2, 6, "tone")}), 2)),
                     Expected);
        ASSERT_EQUAL(EditsOf(ResultOf(P.Run({MakeRename(2, P.Folder.Uri("Shadow.cosmos"), 1, 25, "tone")}), 2)),
                     (std::vector<std::string>{"Shadow.cosmos:1:9=--tone", "Shadow.cosmos:2:22=--tone"}));
    });

    IT("renames a class in stylesheets and in .chaos class strings, outside comments", {
        Project    P;
        const auto Expected = (std::vector<std::string>{"Card.chaos:2:26=panel", "Card.cosmos:1:1=.panel",
                                                        "Card.cosmos:7:1=.panel", "global.cosmos:5:1=.panel"});
        ASSERT_EQUAL(EditsOf(ResultOf(P.Run({MakeRename(2, P.Folder.Uri("Card.chaos"), 1, 27, "panel")}), 2)),
                     Expected);
        ASSERT_EQUAL(EditsOf(ResultOf(P.Run({MakeRename(2, P.Folder.Uri("Card.cosmos"), 0, 2, ".panel")}), 2)),
                     Expected);
    });

    IT("refuses a rename to a name that's taken, that isn't a name, or of a property", {
        Project    P;
        const auto Responses = P.Run({MakeRename(2, P.Folder.Uri("Card.cosmos"), 2, 30, "--brand"),
                                      MakeRename(3, P.Folder.Uri("Card.cosmos"), 2, 30, "inset"),
                                      MakeRename(4, P.Folder.Uri("Card.cosmos"), 0, 2, "title"),
                                      MakeRename(5, P.Folder.Uri("Card.cosmos"), 0, 2, "9lives"),
                                      MakeRename(6, P.Folder.Uri("Card.cosmos"), 3, 6, "margin")});
        ASSERT_EQUAL(ErrorOf(Responses, 2), std::string("`--brand` is already used in global.cosmos, line 2."));
        ASSERT_EQUAL(ErrorOf(Responses, 3), std::string("`--inset` is already used in Card.cosmos, line 2."));
        ASSERT_EQUAL(ErrorOf(Responses, 4), std::string("`.title` is already a class in Card.chaos, line 2."));
        ASSERT_EQUAL(ErrorOf(Responses, 5), std::string("`9lives` isn't a class name."));
        ASSERT_EQUAL(ErrorOf(Responses, 6), std::string("Only a variable or a class can be renamed."));
    });

    IT("offers the checker's suggestions as quick fixes in the requested range", {
        Project         P;
        const std::string Uri    = P.Folder.Uri("Fix.cosmos");
        JsonValue       Whole  = WithParam(MakeRequest(2, "textDocument/codeAction", Uri, 0, 0), "range",
                                           MakeRange0(0, 0, 5, 0));
        JsonValue       OneLine = WithParam(MakeRequest(3, "textDocument/codeAction", Uri, 0, 0), "range",
                                            MakeRange0(2, 14, 2, 14));
        const auto Responses = P.Run({MakeDidOpen(Uri, kFix), Whole, OneLine});

        std::vector<std::string> Fixes;
        const JsonValue          Actions = ResultOf(Responses, 2);
        for (std::size_t I = 0; I < Json::Size(Actions); ++I) {
            const JsonValue& Action = Json::At(Actions, I);
            ASSERT_EQUAL(Json::AsString(Json::Get(Action, "kind")), std::string("quickfix"));
            Fixes.push_back(Json::AsString(Json::Get(Action, "title")) + " " + EditsOf(Json::Get(Action, "edit"))[0]);
        }
        ASSERT_EQUAL(Fixes, (std::vector<std::string>{"Change to `color` Fix.cosmos:2:5=color",
                                                      "Change to `stack` Fix.cosmos:3:14=stack",
                                                      "Change to `var(--accent)` Fix.cosmos:4:12=var(--accent)"}));
        ASSERT_EQUAL(Json::Size(ResultOf(Responses, 3)), static_cast<std::size_t>(1));
    });

    IT("finds hex and rgba() colours, but not var()s", {
        Project           P;
        const std::string Uri  = P.Folder.Uri("Tint.cosmos");
        const std::string Text = ".tint { background-color: rgba(0, 0, 0, 128); color: #f00; border-color: var(--brand); }\n";
        const JsonValue   Colors =
            ResultOf(P.Run({MakeDidOpen(Uri, Text), MakeRequest(2, "textDocument/documentColor", Uri, 0, 0)}), 2);
        REQUIRE_EQUAL(Json::Size(Colors), static_cast<std::size_t>(2));
        ASSERT_EQUAL(Where("Tint.cosmos", Json::Get(Json::At(Colors, 0), "range")), std::string("Tint.cosmos:1:27"));
        ASSERT_EQUAL(IntegerAt(Json::At(Colors, 0), {"range", "end", "character"}), 44LL);
        ASSERT_TRUE(std::abs(NumberAt(Json::At(Colors, 0), {"color", "alpha"}) - 128.0 / 255.0) < 1e-6);
        ASSERT_TRUE(std::abs(NumberAt(Json::At(Colors, 1), {"color", "red"}) - 1.0) < 1e-6);
        ASSERT_TRUE(std::abs(NumberAt(Json::At(Colors, 1), {"color", "green"})) < 1e-6);
    });

    IT("presents a picked colour as hex and as rgb() or rgba()", {
        Project           P;
        const std::string Uri       = P.Folder.Uri("Card.cosmos");
        const auto        Responses = P.Run({MakeColorRequest(2, Uri, 1.0, 0.0, 0.0, 1.0),
                                             MakeColorRequest(3, Uri, 1.0, 0.0, 0.0, 0.5)});
        ASSERT_EQUAL(LabelsOf(ResultOf(Responses, 2)), (std::vector<std::string>{"#ff0000", "rgb(255, 0, 0)"}));
        ASSERT_EQUAL(LabelsOf(ResultOf(Responses, 3)),
                     (std::vector<std::string>{"#ff000080", "rgba(255, 0, 0, 128)"}));
    });

    IT("folds multi-line rules and :root, leaving the closing brace showing", {
        Project    P;
        const auto Responses =
            P.Run({MakeDidOpen(P.Folder.Uri("global.cosmos"), kGlobal),
                   MakeRequest(2, "textDocument/foldingRange", P.Folder.Uri("Card.cosmos"), 0, 0),
                   MakeRequest(3, "textDocument/foldingRange", P.Folder.Uri("global.cosmos"), 0, 0)});
        const JsonValue Card = ResultOf(Responses, 2);
        REQUIRE_EQUAL(Json::Size(Card), static_cast<std::size_t>(1));
        ASSERT_EQUAL(IntegerAt(Json::At(Card, 0), {"startLine"}), 0LL);
        ASSERT_EQUAL(IntegerAt(Json::At(Card, 0), {"endLine"}), 4LL);
        const JsonValue Global = ResultOf(Responses, 3);
        REQUIRE_EQUAL(Json::Size(Global), static_cast<std::size_t>(1));
        ASSERT_EQUAL(IntegerAt(Json::At(Global, 0), {"endLine"}), 2LL);
    });
});
