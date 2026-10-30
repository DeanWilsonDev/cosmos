#include "cimmerian/test.hpp"

#include "ServerHarness.h"

#include <algorithm>

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

const std::string kOther = ".other { color: var(--accent); }\n";

const std::string kChaos = "Card :: (props: CardProps) -> Penumbra:::Component {\n"
                           "    return <Frame class=\"card wide\"><Text class=\"title\">{props.title}</Text></Frame>;\n"
                           "}\n";

JsonValue ResultOf(const std::vector<JsonValue>& Responses, long long Id) {
    for (const JsonValue& Message : Responses) {
        if (Json::Contains(Message, "id") && Json::IsInteger(Json::Get(Message, "id")) &&
            Json::AsInteger(Json::Get(Message, "id")) == Id) {
            return Json::Get(Message, "result");
        }
    }
    return JsonValue();
}

std::string HoverText(const JsonValue& Result) {
    return Json::AsString(Json::Get(Json::Get(Result, "contents"), "value"));
}

JsonValue MakeReferences(int Id, const std::string& Uri, std::uint32_t Line0, std::uint32_t Character0,
                         bool IncludeDeclaration) {
    JsonValue Request = MakeRequest(Id, "textDocument/references", Uri, Line0, Character0);
    JsonValue Context = Json::MakeObject();
    Json::Insert(Context, "includeDeclaration", JsonValue(IncludeDeclaration));
    JsonValue Params = Json::Get(Request, "params");
    Json::Insert(Params, "context", std::move(Context));
    Json::Insert(Request, "params", std::move(Params));
    return Request;
}

std::vector<std::string> LocationsOf(const JsonValue& Result) {
    std::vector<std::string> Out;
    for (std::size_t I = 0; I < Json::Size(Result); ++I) {
        const JsonValue& Location = Json::At(Result, I);
        const std::string Uri = Json::AsString(Json::Get(Location, "uri"));
        Out.push_back(std::filesystem::path(Uri).filename().string() + ":" +
                      std::to_string(IntegerAt(Location, {"range", "start", "line"}) + 1) + ":" +
                      std::to_string(IntegerAt(Location, {"range", "start", "character"}) + 1));
    }
    std::ranges::sort(Out);
    return Out;
}

struct Project {
    ScratchFolder Folder;

    Project() {
        Folder.Write("global.cosmos", kGlobal);
        Folder.Write("Card.cosmos", kCard);
        Folder.Write("Other.cosmos", kOther);
        Folder.Write("Card.chaos", kChaos);
    }

    std::vector<JsonValue> Run(std::vector<JsonValue> Requests) const {
        Requests.insert(Requests.begin(), MakeDidOpen(Folder.Uri("Card.cosmos"), kCard));
        return RunServer(Requests);
    }
};

} // namespace

DESCRIBE("Server navigation", {
    IT("advertises hover, references, highlights and symbols", {
        const auto Responses = RunServer({MakeRequest(1, "initialize", "", 0, 0)});
        const JsonValue& Capabilities = Json::Get(Json::Get(Responses[0], "result"), "capabilities");
        for (const char* Provider : {"hoverProvider", "referencesProvider", "documentHighlightProvider",
                                     "documentSymbolProvider", "workspaceSymbolProvider"}) {
            ASSERT_TRUE(Json::AsBoolean(Json::Get(Capabilities, Provider)));
        }
    });

    IT("hovers a property with its summary and what it takes", {
        Project P;
        const JsonValue Result = ResultOf(P.Run({MakeRequest(2, "textDocument/hover", P.Folder.Uri("Card.cosmos"), 3, 6)}), 2);
        const std::string Text = HoverText(Result);
        ASSERT_TRUE(Text.find("**`padding`**: Space inside the border.") == 0);
        ASSERT_TRUE(Text.find("Takes one to four lengths in `px`") != std::string::npos);
    });

    IT("hovers a variable with its value, followed through var() to global.cosmos", {
        Project P;
        const JsonValue Result = ResultOf(P.Run({MakeRequest(2, "textDocument/hover", P.Folder.Uri("Card.cosmos"), 2, 30)}), 2);
        const std::string Text = HoverText(Result);
        ASSERT_TRUE(Text.find("--accent: var(--brand);") != std::string::npos);
        ASSERT_TRUE(Text.find("= `#4a90ff` (`--brand`)") != std::string::npos);
        ASSERT_TRUE(Text.find("Declared in `global.cosmos`, line 3.") != std::string::npos);
    });

    IT("hovers a class with its rules in cascade order", {
        Project P;
        const JsonValue Result = ResultOf(P.Run({MakeRequest(2, "textDocument/hover", P.Folder.Uri("Card.cosmos"), 0, 2)}), 2);
        const std::string Text = HoverText(Result);
        ASSERT_TRUE(Text.find("**`.card`**: 3 rules, in cascade order:") == 0);
        const auto Global = Text.find("`.card` global.cosmos:5");
        const auto Own    = Text.find("`.card` Card.cosmos:1");
        const auto Hover  = Text.find("`.card:hover` Card.cosmos:7");
        ASSERT_TRUE(Global != std::string::npos && Own != std::string::npos && Hover != std::string::npos);
        ASSERT_TRUE(Global < Own && Own < Hover);
    });

    IT("finds a global variable's uses in every stylesheet beside it", {
        Project P;
        const JsonValue Result =
            ResultOf(P.Run({MakeReferences(2, P.Folder.Uri("Card.cosmos"), 2, 30, true)}), 2);
        ASSERT_EQUAL(LocationsOf(Result), (std::vector<std::string>{"Card.cosmos:3:27", "Card.cosmos:5:29",
                                                                    "Other.cosmos:1:21", "global.cosmos:3:5"}));
        const JsonValue UsesOnly =
            ResultOf(P.Run({MakeReferences(2, P.Folder.Uri("Card.cosmos"), 2, 30, false)}), 2);
        ASSERT_EQUAL(Json::Size(UsesOnly), static_cast<std::size_t>(3));
    });

    IT("leaves out files that declare their own copy of a global variable", {
        Project P;
        P.Folder.Write("Shadow.cosmos", ":root { --accent: #000000; }\n.shadow { color: var(--accent); }\n");
        const auto Expected = (std::vector<std::string>{"Card.cosmos:3:27", "Card.cosmos:5:29", "Other.cosmos:1:21",
                                                        "global.cosmos:3:5"});
        ASSERT_EQUAL(LocationsOf(ResultOf(P.Run({MakeReferences(2, P.Folder.Uri("global.cosmos"), 2, 6, true)}), 2)),
                     Expected);
        ASSERT_EQUAL(LocationsOf(ResultOf(P.Run({MakeReferences(2, P.Folder.Uri("Other.cosmos"), 0, 22, true)}), 2)),
                     Expected);
        ASSERT_EQUAL(LocationsOf(ResultOf(P.Run({MakeReferences(2, P.Folder.Uri("Shadow.cosmos"), 1, 25, true)}), 2)),
                     (std::vector<std::string>{"Shadow.cosmos:1:9", "Shadow.cosmos:2:22"}));
    });

    IT("keeps a variable the file declares itself to that file", {
        Project P;
        const JsonValue Result = ResultOf(P.Run({MakeReferences(2, P.Folder.Uri("Card.cosmos"), 3, 20, true)}), 2);
        ASSERT_EQUAL(LocationsOf(Result), (std::vector<std::string>{"Card.cosmos:2:5", "Card.cosmos:4:18"}));
    });

    IT("finds a class's selectors and its uses in .chaos files, from either side", {
        Project P;
        const auto Expected = (std::vector<std::string>{"Card.chaos:2:26", "Card.cosmos:1:1", "Card.cosmos:7:1",
                                                        "global.cosmos:5:1"});
        ASSERT_EQUAL(LocationsOf(ResultOf(P.Run({MakeReferences(2, P.Folder.Uri("Card.cosmos"), 0, 2, true)}), 2)),
                     Expected);
        ASSERT_EQUAL(LocationsOf(ResultOf(P.Run({MakeReferences(2, P.Folder.Uri("Card.chaos"), 1, 27, true)}), 2)),
                     Expected);
        ASSERT_EQUAL(LocationsOf(ResultOf(P.Run({MakeReferences(2, P.Folder.Uri("Card.cosmos"), 0, 2, false)}), 2)),
                     (std::vector<std::string>{"Card.chaos:2:26"}));
    });

    IT("skips class strings in comments and in build folders, but not in folders named like them", {
        Project P;
        P.Folder.Write("Commented.chaos", "// <Frame class=\"card\"/>\n/* class=\"card\" */\n");
        P.Folder.Write("build/Built.chaos", "<Frame class=\"card\"/>\n");
        P.Folder.Write("buildings/House.chaos", "<Frame class=\"card\"/>\n");
        ASSERT_EQUAL(LocationsOf(ResultOf(P.Run({MakeReferences(2, P.Folder.Uri("Card.cosmos"), 0, 2, false)}), 2)),
                     (std::vector<std::string>{"Card.chaos:2:26", "House.chaos:1:15"}));
    });

    IT("answers only references for an open .chaos buffer, and never parses it as Cosmos", {
        Project   P;
        const std::string ChaosUri  = P.Folder.Uri("Card.chaos");
        const auto        Responses = P.Run({MakeDidOpen(ChaosUri, kChaos),
                                             MakeRequest(2, "textDocument/hover", ChaosUri, 1, 27),
                                             MakeRequest(3, "textDocument/completion", ChaosUri, 1, 27),
                                             MakeReferences(4, ChaosUri, 1, 27, true)});
        ASSERT_TRUE(PublishedFor(Responses, ChaosUri).empty());
        ASSERT_TRUE(Json::IsNull(ResultOf(Responses, 2)));
        ASSERT_TRUE(Json::IsNull(ResultOf(Responses, 3)));
        ASSERT_EQUAL(Json::Size(ResultOf(Responses, 4)), static_cast<std::size_t>(4));
    });

    IT("highlights a variable in the file, marking its declaration", {
        Project P;
        const JsonValue Result =
            ResultOf(P.Run({MakeRequest(2, "textDocument/documentHighlight", P.Folder.Uri("Card.cosmos"), 1, 6)}), 2);
        REQUIRE_EQUAL(Json::Size(Result), static_cast<std::size_t>(2));
        ASSERT_EQUAL(IntegerAt(Json::At(Result, 0), {"kind"}), 3LL);
        ASSERT_EQUAL(IntegerAt(Json::At(Result, 1), {"kind"}), 2LL);
    });

    IT("outlines rules, nested rules and variables", {
        Project P;
        const JsonValue Result =
            ResultOf(P.Run({MakeRequest(2, "textDocument/documentSymbol", P.Folder.Uri("Card.cosmos"), 0, 0)}), 2);
        REQUIRE_EQUAL(Json::Size(Result), static_cast<std::size_t>(2));
        const JsonValue& Card = Json::At(Result, 0);
        ASSERT_EQUAL(Json::AsString(Json::Get(Card, "name")), std::string(".card"));
        ASSERT_EQUAL(IntegerAt(Card, {"range", "end", "line"}), 5LL);
        ASSERT_EQUAL(IntegerAt(Card, {"selectionRange", "end", "character"}), 5LL);
        const JsonValue& Children = Json::Get(Card, "children");
        REQUIRE_EQUAL(Json::Size(Children), static_cast<std::size_t>(2));
        ASSERT_EQUAL(Json::AsString(Json::Get(Json::At(Children, 0), "name")), std::string("--inset"));
        ASSERT_EQUAL(Json::AsString(Json::Get(Json::At(Children, 0), "detail")), std::string("8px"));
        ASSERT_EQUAL(Json::AsString(Json::Get(Json::At(Children, 1), "name")), std::string("text:hover"));
        ASSERT_EQUAL(Json::AsString(Json::Get(Json::At(Result, 1), "name")), std::string(".card:hover"));
    });

    IT("lists the workspace's classes and variables, filtered by the query", {
        Project   P;
        JsonValue Initialize = MakeRequest(1, "initialize", "", 0, 0);
        JsonValue Params     = Json::MakeObject();
        Json::Insert(Params, "rootUri", JsonValue(P.Folder.Uri("")));
        Json::Insert(Initialize, "params", std::move(Params));
        JsonValue Query       = MakeNotification("workspace/symbol", Json::MakeObject());
        JsonValue QueryParams = Json::MakeObject();
        Json::Insert(QueryParams, "query", JsonValue("CARD"));
        Json::Insert(Query, "params", std::move(QueryParams));
        Json::Insert(Query, "id", JsonValue(static_cast<long long>(2)));

        const JsonValue Result = ResultOf(P.Run({Initialize, Query}), 2);
        std::vector<std::string> Names;
        for (std::size_t I = 0; I < Json::Size(Result); ++I) {
            Names.push_back(Json::AsString(Json::Get(Json::At(Result, I), "name")) + " " +
                            Json::AsString(Json::Get(Json::At(Result, I), "containerName")));
        }
        ASSERT_EQUAL(Names, (std::vector<std::string>{".card Card.cosmos", ".card global.cosmos"}));
    });
});
