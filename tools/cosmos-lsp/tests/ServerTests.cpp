#include "cimmerian/test.hpp"

#include "ServerHarness.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace CosmosLsp;
using namespace CosmosLspTests;

namespace {

const std::string kGlobalWithAccent = ":root {\n    --accent: #4a90ff;\n}\n";
const std::string kCardUsingAccent  = ".card {\n    background-color: var(--accent);\n}\n";

} // namespace

DESCRIBE("Server", {
    IT("replies to initialize with completion and definition capabilities", {
        const auto Responses = RunServer({MakeRequest(1, "initialize", "", 0, 0)});
        REQUIRE_TRUE(!Responses.empty());
        const auto& Capabilities = Amanuensis::Json::Get(Amanuensis::Json::Get(Responses[0], "result"), "capabilities");
        ASSERT_TRUE(Amanuensis::Json::AsBoolean(Amanuensis::Json::Get(Capabilities, "definitionProvider")));
        ASSERT_FALSE(Amanuensis::Json::IsNull(Amanuensis::Json::Get(Capabilities, "completionProvider")));
    });

    IT("publishes a diagnostic for a compound tag+class selector", {
        const std::string Uri = "file:///Bad.cosmos";
        const auto Responses = RunServer({MakeDidOpen(Uri, "Frame.card {\n    padding: 8px;\n}\n")});

        bool Found = false;
        for (const auto& Msg : Responses) {
            if (Amanuensis::Json::Contains(Msg, "method") &&
                Amanuensis::Json::AsString(Amanuensis::Json::Get(Msg, "method")) == "textDocument/publishDiagnostics") {
                const auto& Diags = Amanuensis::Json::Get(Amanuensis::Json::Get(Msg, "params"), "diagnostics");
                Found = Amanuensis::Json::Size(Diags) > 0;
            }
        }
        ASSERT_TRUE(Found);
    });

    IT("offers property names when completing inside a rule block", {
        const std::string Uri = "file:///Card.cosmos";
        const auto Responses = RunServer({
            MakeDidOpen(Uri, ".card {\n    \n}\n"),
            MakeRequest(2, "textDocument/completion", Uri, 1, 4),
        });

        REQUIRE_TRUE(Responses.size() >= 2);
        const auto& Items = Amanuensis::Json::Get(Amanuensis::Json::Get(Responses.back(), "result"), "items");
        bool SawBackgroundColor = false;
        for (std::size_t I = 0; I < Amanuensis::Json::Size(Items); ++I) {
            if (Amanuensis::Json::AsString(Amanuensis::Json::Get(Amanuensis::Json::At(Items, I), "label")) ==
                "background-color") {
                SawBackgroundColor = true;
            }
        }
        ASSERT_TRUE(SawBackgroundColor);
    });

    IT("goto-definition on a var(--name) reference jumps to its declaration", {
        const std::string Uri = "file:///HealthBar.cosmos";
        const std::string Text = ".health-bar {\n"
                                   "    --bar-background: #333333;\n"
                                   "\n"
                                   "    background-color: var(--bar-background);\n"
                                   "}\n";
        const auto Responses = RunServer({
            MakeDidOpen(Uri, Text),
            MakeRequest(3, "textDocument/definition", Uri, 3, 30), // inside "--bar-background" reference
        });

        REQUIRE_TRUE(Responses.size() >= 2);
        const auto& Result = Amanuensis::Json::Get(Responses.back(), "result");
        REQUIRE_TRUE(!Amanuensis::Json::IsNull(Result));
        ASSERT_EQUAL(static_cast<std::uint32_t>(Amanuensis::Json::AsInteger(
                         Amanuensis::Json::Get(Amanuensis::Json::Get(Amanuensis::Json::Get(Result, "range"), "start"),
                                                "line"))),
                     static_cast<std::uint32_t>(1)); // 0-based line 1 == source line 2
    });

    IT("underlines the token a parse error points at", {
        const std::string Uri = "file:///Bad.cosmos";
        const auto Published = PublishedFor(RunServer({MakeDidOpen(Uri, ".card {\n    padding 8px;\n}\n")}), Uri);
        REQUIRE_EQUAL(Published.size(), static_cast<std::size_t>(1));
        REQUIRE_TRUE(Amanuensis::Json::Size(Published[0]) > 0);
        const auto& First = Amanuensis::Json::At(Published[0], 0);
        ASSERT_EQUAL(IntegerAt(First, {"range", "start", "character"}), 4LL);
        ASSERT_EQUAL(IntegerAt(First, {"range", "end", "character"}), 11LL);
    });

    IT("publishes the checker's findings with their ranges and severities", {
        const std::string Uri = "file:///Card.cosmos";
        const auto Published =
            PublishedFor(RunServer({MakeDidOpen(Uri, ".card {\n    colr: #fff;\n    display: blok;\n}\n")}), Uri);
        REQUIRE_EQUAL(Published.size(), static_cast<std::size_t>(1));
        REQUIRE_EQUAL(Amanuensis::Json::Size(Published[0]), static_cast<std::size_t>(2));
        const auto& Unknown = Amanuensis::Json::At(Published[0], 0);
        ASSERT_EQUAL(IntegerAt(Unknown, {"severity"}), 2LL);
        ASSERT_EQUAL(IntegerAt(Unknown, {"range", "start", "line"}), 1LL);
        ASSERT_EQUAL(IntegerAt(Unknown, {"range", "start", "character"}), 4LL);
        ASSERT_EQUAL(IntegerAt(Unknown, {"range", "end", "character"}), 8LL);
        const auto& Keyword = Amanuensis::Json::At(Published[0], 1);
        ASSERT_EQUAL(IntegerAt(Keyword, {"severity"}), 1LL);
        ASSERT_TRUE(MessageOf(Keyword).find("`display` takes `stack` or `inline`") != std::string::npos);
    });

    IT("checks variables against the global.cosmos beside the file", {
        ScratchFolder Folder;
        Folder.Write("global.cosmos", kGlobalWithAccent);
        const std::string Uri = Folder.Uri("Card.cosmos");
        const auto Published = PublishedFor(
            RunServer({MakeDidOpen(Uri, ".card {\n    background-color: var(--accent);\n    color: var(--ink);\n}\n")}),
            Uri);
        REQUIRE_EQUAL(Amanuensis::Json::Size(Published.back()), static_cast<std::size_t>(1));
        ASSERT_TRUE(MessageOf(Amanuensis::Json::At(Published.back(), 0)).find("`var(--ink)`") != std::string::npos);
    });

    IT("reads an open global.cosmos buffer over the copy on disk, and rechecks when it changes", {
        ScratchFolder Folder;
        Folder.Write("global.cosmos", ":root {\n}\n");
        const std::string GlobalUri = Folder.Uri("global.cosmos");
        const std::string CardUri   = Folder.Uri("Card.cosmos");
        const auto Published = PublishedFor(RunServer({
                                                MakeDidOpen(GlobalUri, kGlobalWithAccent),
                                                MakeDidOpen(CardUri, kCardUsingAccent),
                                                MakeDidChange(GlobalUri, ":root {\n}\n"),
                                            }),
                                            CardUri);
        REQUIRE_EQUAL(Published.size(), static_cast<std::size_t>(2));
        ASSERT_EQUAL(Amanuensis::Json::Size(Published[0]), static_cast<std::size_t>(0));
        ASSERT_EQUAL(Amanuensis::Json::Size(Published[1]), static_cast<std::size_t>(1));
    });

    IT("rechecks open files when a closed global.cosmos changes on disk", {
        ScratchFolder Folder;
        const std::string CardUri = Folder.Uri("Card.cosmos");
        Folder.Write("global.cosmos", ":root {\n}\n");
        std::vector<Amanuensis::JsonValue> Requests{MakeDidOpen(CardUri, kCardUsingAccent)};
        const auto Before = PublishedFor(RunServer(Requests), CardUri);
        ASSERT_EQUAL(Amanuensis::Json::Size(Before.back()), static_cast<std::size_t>(1));

        Folder.Write("global.cosmos", kGlobalWithAccent);
        Requests.push_back(MakeWatchedFileChange(Folder.Uri("global.cosmos")));
        const auto After = PublishedFor(RunServer(Requests), CardUri);
        REQUIRE_EQUAL(After.size(), static_cast<std::size_t>(2));
        ASSERT_EQUAL(Amanuensis::Json::Size(After.back()), static_cast<std::size_t>(0));
    });

    IT("asks a client that can watch files to watch global.cosmos", {
        Amanuensis::JsonValue Capabilities = Amanuensis::Json::MakeObject();
        Amanuensis::JsonValue Workspace = Amanuensis::Json::MakeObject();
        Amanuensis::JsonValue Watched = Amanuensis::Json::MakeObject();
        Amanuensis::Json::Insert(Watched, "dynamicRegistration", Amanuensis::JsonValue(true));
        Amanuensis::Json::Insert(Workspace, "didChangeWatchedFiles", std::move(Watched));
        Amanuensis::Json::Insert(Capabilities, "workspace", std::move(Workspace));
        Amanuensis::JsonValue Params = Amanuensis::Json::MakeObject();
        Amanuensis::Json::Insert(Params, "capabilities", std::move(Capabilities));
        Amanuensis::JsonValue Initialize = MakeNotification("initialize", std::move(Params));
        Amanuensis::Json::Insert(Initialize, "id", Amanuensis::JsonValue(static_cast<long long>(1)));

        const auto Responses =
            RunServer({Initialize, MakeNotification("initialized", Amanuensis::Json::MakeObject())});
        REQUIRE_EQUAL(Responses.size(), static_cast<std::size_t>(2));
        ASSERT_EQUAL(Amanuensis::Json::AsString(Amanuensis::Json::Get(Responses[1], "method")),
                     std::string("client/registerCapability"));

        const auto Plain = RunServer({MakeRequest(1, "initialize", "", 0, 0),
                                      MakeNotification("initialized", Amanuensis::Json::MakeObject())});
        ASSERT_EQUAL(Plain.size(), static_cast<std::size_t>(1));
    });

    IT("offers justify-content's keywords as values", {
        const std::string Uri = "file:///Card.cosmos";
        const auto Responses = RunServer({
            MakeDidOpen(Uri, ".card {\n    justify-content: \n}\n"),
            MakeRequest(2, "textDocument/completion", Uri, 1, 21),
        });
        const auto& Items = Amanuensis::Json::Get(Amanuensis::Json::Get(Responses.back(), "result"), "items");
        ASSERT_EQUAL(Amanuensis::Json::Size(Items), static_cast<std::size_t>(4));
    });

    IT("keeps a finding about global.cosmos's own text out of another file's diagnostics", {
        ScratchFolder Folder;
        Folder.Write("global.cosmos", ":root {\n    --a: var(--missing);\n    --b: var(--c);\n    --c: var(--b);\n}\n");
        const std::string Uri = Folder.Uri("Card.cosmos");
        const auto Published = PublishedFor(
            RunServer({MakeDidOpen(Uri, ".card {\n    color: var(--a);\n    border-color: var(--b);\n}\n")}), Uri);
        REQUIRE_EQUAL(Amanuensis::Json::Size(Published.back()), static_cast<std::size_t>(2));
        ASSERT_EQUAL(IntegerAt(Amanuensis::Json::At(Published.back(), 0), {"range", "start", "line"}), 1LL);
        ASSERT_EQUAL(IntegerAt(Amanuensis::Json::At(Published.back(), 1), {"range", "start", "line"}), 2LL);
        ASSERT_TRUE(MessageOf(Amanuensis::Json::At(Published.back(), 1)).find("refers to itself") != std::string::npos);
    });
});

