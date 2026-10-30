#include "cimmerian/test.hpp"

#include "JsonRpc.h"
#include "Server.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace CosmosLsp;

namespace {

// Feeds Requests (already-framed JSON-RPC messages) into a Server via a temp file
// standing in for stdin, capturing every message it writes to a second temp file
// standing in for stdout. Server::Run exits once it has processed every request
// (mirroring how a real client would end the session with "exit").
std::vector<Amanuensis::JsonValue> RunServer(const std::vector<Amanuensis::JsonValue>& Requests) {
    std::FILE* In = std::tmpfile();
    for (const auto& Req : Requests) {
        JsonRpc::WriteMessage(In, Req);
    }
    Amanuensis::JsonValue Exit = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Exit, "jsonrpc", Amanuensis::JsonValue("2.0"));
    Amanuensis::Json::Insert(Exit, "method", Amanuensis::JsonValue("exit"));
    JsonRpc::WriteMessage(In, Exit);
    std::rewind(In);

    std::FILE* Out = std::tmpfile();
    Server ThisServer;
    ThisServer.Run(In, Out);
    std::fclose(In);

    std::rewind(Out);
    std::vector<Amanuensis::JsonValue> Responses;
    while (const auto Msg = JsonRpc::ReadMessage(Out)) {
        Responses.push_back(*Msg);
    }
    std::fclose(Out);
    return Responses;
}

Amanuensis::JsonValue MakeDidOpen(const std::string& Uri, const std::string& Text) {
    Amanuensis::JsonValue TextDocument = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(TextDocument, "uri", Amanuensis::JsonValue(Uri));
    Amanuensis::Json::Insert(TextDocument, "text", Amanuensis::JsonValue(Text));
    Amanuensis::JsonValue Params = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Params, "textDocument", std::move(TextDocument));
    Amanuensis::JsonValue Message = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Message, "jsonrpc", Amanuensis::JsonValue("2.0"));
    Amanuensis::Json::Insert(Message, "method", Amanuensis::JsonValue("textDocument/didOpen"));
    Amanuensis::Json::Insert(Message, "params", std::move(Params));
    return Message;
}

Amanuensis::JsonValue MakeRequest(int Id, const std::string& Method, const std::string& Uri, std::uint32_t Line0,
                                std::uint32_t Character0) {
    Amanuensis::JsonValue TextDocument = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(TextDocument, "uri", Amanuensis::JsonValue(Uri));
    Amanuensis::JsonValue Position = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Position, "line", Amanuensis::JsonValue(static_cast<long long>(Line0)));
    Amanuensis::Json::Insert(Position, "character", Amanuensis::JsonValue(static_cast<long long>(Character0)));
    Amanuensis::JsonValue Params = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Params, "textDocument", std::move(TextDocument));
    Amanuensis::Json::Insert(Params, "position", std::move(Position));
    Amanuensis::JsonValue Message = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Message, "jsonrpc", Amanuensis::JsonValue("2.0"));
    Amanuensis::Json::Insert(Message, "id", Amanuensis::JsonValue(static_cast<long long>(Id)));
    Amanuensis::Json::Insert(Message, "method", Amanuensis::JsonValue(Method));
    Amanuensis::Json::Insert(Message, "params", std::move(Params));
    return Message;
}

Amanuensis::JsonValue MakeNotification(const std::string& Method, Amanuensis::JsonValue Params) {
    Amanuensis::JsonValue Message = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Message, "jsonrpc", Amanuensis::JsonValue("2.0"));
    Amanuensis::Json::Insert(Message, "method", Amanuensis::JsonValue(Method));
    Amanuensis::Json::Insert(Message, "params", std::move(Params));
    return Message;
}

Amanuensis::JsonValue MakeDidChange(const std::string& Uri, const std::string& Text) {
    Amanuensis::JsonValue TextDocument = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(TextDocument, "uri", Amanuensis::JsonValue(Uri));
    Amanuensis::JsonValue Change = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Change, "text", Amanuensis::JsonValue(Text));
    Amanuensis::JsonValue Changes = Amanuensis::Json::MakeArray();
    Amanuensis::Json::PushBack(Changes, std::move(Change));
    Amanuensis::JsonValue Params = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Params, "textDocument", std::move(TextDocument));
    Amanuensis::Json::Insert(Params, "contentChanges", std::move(Changes));
    return MakeNotification("textDocument/didChange", std::move(Params));
}

Amanuensis::JsonValue MakeWatchedFileChange(const std::string& Uri) {
    Amanuensis::JsonValue Change = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Change, "uri", Amanuensis::JsonValue(Uri));
    Amanuensis::Json::Insert(Change, "type", Amanuensis::JsonValue(static_cast<long long>(2)));
    Amanuensis::JsonValue Changes = Amanuensis::Json::MakeArray();
    Amanuensis::Json::PushBack(Changes, std::move(Change));
    Amanuensis::JsonValue Params = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Params, "changes", std::move(Changes));
    return MakeNotification("workspace/didChangeWatchedFiles", std::move(Params));
}

// Every publishDiagnostics the server sent for Uri, in order; each entry is that
// notification's diagnostics array.
std::vector<Amanuensis::JsonValue> PublishedFor(const std::vector<Amanuensis::JsonValue>& Responses,
                                                const std::string& Uri) {
    std::vector<Amanuensis::JsonValue> Published;
    for (const auto& Msg : Responses) {
        if (!Amanuensis::Json::Contains(Msg, "method") ||
            Amanuensis::Json::AsString(Amanuensis::Json::Get(Msg, "method")) != "textDocument/publishDiagnostics") {
            continue;
        }
        const auto& Params = Amanuensis::Json::Get(Msg, "params");
        if (Amanuensis::Json::AsString(Amanuensis::Json::Get(Params, "uri")) == Uri) {
            Published.push_back(Amanuensis::Json::Get(Params, "diagnostics"));
        }
    }
    return Published;
}

long long IntegerAt(const Amanuensis::JsonValue& Value, std::initializer_list<const char*> Keys) {
    const Amanuensis::JsonValue* Current = &Value;
    for (const char* Key : Keys) {
        Current = &Amanuensis::Json::Get(*Current, Key);
    }
    return Amanuensis::Json::AsInteger(*Current);
}

std::string MessageOf(const Amanuensis::JsonValue& Diagnostic) {
    return Amanuensis::Json::AsString(Amanuensis::Json::Get(Diagnostic, "message"));
}

class ScratchFolder {
public:
    ScratchFolder() {
        static int Counter = 0;
        Path_ = std::filesystem::temp_directory_path() /
                ("cosmos-lsp-tests-" + std::to_string(::getpid()) + "-" + std::to_string(++Counter));
        std::filesystem::create_directories(Path_);
    }
    ~ScratchFolder() { std::filesystem::remove_all(Path_); }

    void Write(const std::string& Name, const std::string& Text) const { std::ofstream(Path_ / Name) << Text; }
    std::string Uri(const std::string& Name) const { return "file://" + (Path_ / Name).string(); }

private:
    std::filesystem::path Path_;
};

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

