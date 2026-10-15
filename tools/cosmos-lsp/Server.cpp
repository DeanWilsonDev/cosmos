#include "Server.h"

#include "CompletionContext.h"
#include "JsonRpc.h"
#include "Cosmos/Parser.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace CosmosLsp {

namespace {

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

Amanuensis::JsonValue MakeRange(std::uint32_t Line, std::uint32_t Column) {
    Amanuensis::JsonValue Range = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Range, "start", MakePosition(Line, Column));
    Amanuensis::Json::Insert(Range, "end", MakePosition(Line, Column));
    return Range;
}

std::pair<std::uint32_t, std::uint32_t> PositionFromParams(const Amanuensis::JsonValue& Position) {
    return {static_cast<std::uint32_t>(Amanuensis::Json::AsInteger(Amanuensis::Json::Get(Position, "line"))) + 1,
            static_cast<std::uint32_t>(Amanuensis::Json::AsInteger(Amanuensis::Json::Get(Position, "character"))) +
                1};
}

Amanuensis::JsonValue MakeCompletionItem(const std::string& Label, int Kind, const std::string& Detail = "") {
    Amanuensis::JsonValue Item = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Item, "label", Amanuensis::JsonValue(Label));
    Amanuensis::Json::Insert(Item, "kind", Amanuensis::JsonValue(static_cast<long long>(Kind)));
    if (!Detail.empty()) {
        Amanuensis::Json::Insert(Item, "detail", Amanuensis::JsonValue(Detail));
    }
    return Item;
}

// LSP CompletionItemKind values used below (microsoft/language-server-protocol).
constexpr int kCompletionKindProperty = 10;
constexpr int kCompletionKindClass = 7;
constexpr int kCompletionKindKeyword = 14;
constexpr int kCompletionKindVariable = 6;

// A stylesheet's own sibling `global.cosmos`, if it exists and isn't the file itself --
// the other half of §1.3's two-layer cascade. Read fresh each time rather than cached:
// completion/goto-definition are not latency-sensitive enough here to justify tracking
// global.cosmos as its own open-or-watched document.
std::optional<Cosmos::Stylesheet> LoadSiblingGlobalSheet(const std::string& Path) {
    const std::filesystem::path GlobalPath = std::filesystem::path(Path).parent_path() / "global.cosmos";
    if (std::filesystem::path(Path).filename() == "global.cosmos") {
        return std::nullopt;
    }
    const auto Text = ReadFile(GlobalPath);
    if (!Text) {
        return std::nullopt;
    }
    Cosmos::Parser     P(*Text, GlobalPath.string());
    Cosmos::ParseResult Result = P.Parse();
    return std::move(Result.Sheet);
}

} // namespace

void Server::Run(std::FILE* In, std::FILE* Out) {
    Out_ = Out;
    for (;;) {
        const std::optional<Amanuensis::JsonValue> Message = JsonRpc::ReadMessage(In);
        if (!Message) {
            return;
        }
        const std::string Method = Amanuensis::Json::IsObject(*Message) && Amanuensis::Json::Contains(*Message, "method")
                                        ? Amanuensis::Json::AsString(Amanuensis::Json::Get(*Message, "method"))
                                        : std::string{};
        HandleMessage(*Message);
        if (Method == "exit") {
            return;
        }
    }
}

void Server::Reply(const Amanuensis::JsonValue& Id, Amanuensis::JsonValue Result) {
    Amanuensis::JsonValue Message = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Message, "jsonrpc", Amanuensis::JsonValue("2.0"));
    Amanuensis::Json::Insert(Message, "id", Id);
    Amanuensis::Json::Insert(Message, "result", std::move(Result));
    JsonRpc::WriteMessage(Out_, Message);
}

void Server::Notify(const std::string& Method, Amanuensis::JsonValue Params) {
    Amanuensis::JsonValue Message = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Message, "jsonrpc", Amanuensis::JsonValue("2.0"));
    Amanuensis::Json::Insert(Message, "method", Amanuensis::JsonValue(Method));
    Amanuensis::Json::Insert(Message, "params", std::move(Params));
    JsonRpc::WriteMessage(Out_, Message);
}

void Server::HandleMessage(const Amanuensis::JsonValue& Message) {
    if (!Amanuensis::Json::IsObject(Message) || !Amanuensis::Json::Contains(Message, "method")) {
        return;
    }
    const std::string       Method = Amanuensis::Json::AsString(Amanuensis::Json::Get(Message, "method"));
    const Amanuensis::JsonValue Params =
        Amanuensis::Json::Contains(Message, "params") ? Amanuensis::Json::Get(Message, "params") : Amanuensis::JsonValue();
    const bool               IsRequest = Amanuensis::Json::Contains(Message, "id");
    const Amanuensis::JsonValue  Id = IsRequest ? Amanuensis::Json::Get(Message, "id") : Amanuensis::JsonValue();

    if (Method == "initialize") {
        HandleInitialize(Id, Params);
    } else if (Method == "initialized" || Method == "$/setTrace" || Method == "workspace/didChangeConfiguration") {
        // Accepted, no action needed.
    } else if (Method == "shutdown") {
        Reply(Id, Amanuensis::JsonValue());
    } else if (Method == "exit") {
        // Handled by Run()'s own loop after this returns.
    } else if (Method == "textDocument/didOpen") {
        HandleDidOpen(Params);
    } else if (Method == "textDocument/didChange") {
        HandleDidChange(Params);
    } else if (Method == "textDocument/didClose") {
        HandleDidClose(Params);
    } else if (Method == "textDocument/completion") {
        HandleCompletion(Id, Params);
    } else if (Method == "textDocument/definition") {
        HandleDefinition(Id, Params);
    } else if (IsRequest) {
        Amanuensis::JsonValue Error = Amanuensis::Json::MakeObject();
        Amanuensis::Json::Insert(Error, "code", Amanuensis::JsonValue(static_cast<long long>(-32601)));
        Amanuensis::Json::Insert(Error, "message", Amanuensis::JsonValue("method not found: " + Method));
        Amanuensis::JsonValue Response = Amanuensis::Json::MakeObject();
        Amanuensis::Json::Insert(Response, "jsonrpc", Amanuensis::JsonValue("2.0"));
        Amanuensis::Json::Insert(Response, "id", Id);
        Amanuensis::Json::Insert(Response, "error", std::move(Error));
        JsonRpc::WriteMessage(Out_, Response);
    }
    // An unknown notification (no id) is silently ignored, per LSP's own "must not
    // fail" guidance for messages a server doesn't recognise.
}

void Server::HandleInitialize(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& /*Params*/) {
    Amanuensis::JsonValue Completion = Amanuensis::Json::MakeObject();
    Amanuensis::JsonValue TriggerChars = Amanuensis::Json::MakeArray();
    Amanuensis::Json::PushBack(TriggerChars, Amanuensis::JsonValue(":"));
    Amanuensis::Json::PushBack(TriggerChars, Amanuensis::JsonValue("("));
    Amanuensis::Json::PushBack(TriggerChars, Amanuensis::JsonValue(" "));
    Amanuensis::Json::PushBack(TriggerChars, Amanuensis::JsonValue("."));
    Amanuensis::Json::Insert(Completion, "triggerCharacters", std::move(TriggerChars));

    Amanuensis::JsonValue Capabilities = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Capabilities, "textDocumentSync", Amanuensis::JsonValue(static_cast<long long>(1))); // Full
    Amanuensis::Json::Insert(Capabilities, "completionProvider", std::move(Completion));
    Amanuensis::Json::Insert(Capabilities, "definitionProvider", Amanuensis::JsonValue(true));

    Amanuensis::JsonValue ServerInfo = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(ServerInfo, "name", Amanuensis::JsonValue("cosmos-lsp"));
    Amanuensis::Json::Insert(ServerInfo, "version", Amanuensis::JsonValue("0.1.0"));

    Amanuensis::JsonValue Result = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Result, "capabilities", std::move(Capabilities));
    Amanuensis::Json::Insert(Result, "serverInfo", std::move(ServerInfo));
    Reply(Id, std::move(Result));
}

void Server::HandleDidOpen(const Amanuensis::JsonValue& Params) {
    const Amanuensis::JsonValue& TextDocument = Amanuensis::Json::Get(Params, "textDocument");
    RebuildDocument(Amanuensis::Json::AsString(Amanuensis::Json::Get(TextDocument, "uri")),
                     Amanuensis::Json::AsString(Amanuensis::Json::Get(TextDocument, "text")));
}

void Server::HandleDidChange(const Amanuensis::JsonValue& Params) {
    const Amanuensis::JsonValue& TextDocument = Amanuensis::Json::Get(Params, "textDocument");
    const Amanuensis::JsonValue& Changes = Amanuensis::Json::Get(Params, "contentChanges");
    if (Amanuensis::Json::Size(Changes) == 0) {
        return;
    }
    // Full-document sync only (textDocumentSync=1 in our own capabilities) -- the last
    // entry always carries the complete new text.
    RebuildDocument(Amanuensis::Json::AsString(Amanuensis::Json::Get(TextDocument, "uri")),
                     Amanuensis::Json::AsString(Amanuensis::Json::Get(
                         Amanuensis::Json::At(Changes, Amanuensis::Json::Size(Changes) - 1), "text")));
}

void Server::HandleDidClose(const Amanuensis::JsonValue& Params) {
    Documents_.erase(
        Amanuensis::Json::AsString(Amanuensis::Json::Get(Amanuensis::Json::Get(Params, "textDocument"), "uri")));
}

void Server::RebuildDocument(const std::string& Uri, std::string Text) {
    const std::string Path = UriToPath(Uri);

    OpenDocument& Doc = Documents_[Uri];
    Doc.Text = std::move(Text);

    Cosmos::Parser P(Doc.Text, Path);
    Doc.Parsed = P.Parse();

    PublishDiagnostics(Uri, Doc);
}

void Server::PublishDiagnostics(const std::string& Uri, const OpenDocument& Doc) {
    Amanuensis::JsonValue Diagnostics = Amanuensis::Json::MakeArray();
    for (const Cosmos::ParseError& Err : Doc.Parsed.Errors) {
        Amanuensis::JsonValue Diagnostic = Amanuensis::Json::MakeObject();
        Amanuensis::Json::Insert(Diagnostic, "range", MakeRange(Err.Location.Line, Err.Location.Column));
        Amanuensis::Json::Insert(Diagnostic, "severity", Amanuensis::JsonValue(static_cast<long long>(1))); // Error
        Amanuensis::Json::Insert(Diagnostic, "source", Amanuensis::JsonValue("cosmos"));
        Amanuensis::Json::Insert(Diagnostic, "message", Amanuensis::JsonValue(Err.Message));
        Amanuensis::Json::PushBack(Diagnostics, std::move(Diagnostic));
    }

    Amanuensis::JsonValue Params = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Params, "uri", Amanuensis::JsonValue(Uri));
    Amanuensis::Json::Insert(Params, "diagnostics", std::move(Diagnostics));
    Notify("textDocument/publishDiagnostics", std::move(Params));
}

void Server::HandleCompletion(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params) {
    const std::string Uri =
        Amanuensis::Json::AsString(Amanuensis::Json::Get(Amanuensis::Json::Get(Params, "textDocument"), "uri"));
    const auto [Line, Column] = PositionFromParams(Amanuensis::Json::Get(Params, "position"));

    Amanuensis::JsonValue Items = Amanuensis::Json::MakeArray();
    const auto DocIt = Documents_.find(Uri);
    if (DocIt != Documents_.end()) {
        const OpenDocument&     Doc = DocIt->second;
        const CompletionContext Context = ClassifyCompletionContext(Doc.Text, Line, Column);

        if (Context.Kind == CompletionContextKind::VarRef) {
            for (const auto* V : CollectInScopeVariables(*Doc.Parsed.Sheet)) {
                Amanuensis::Json::PushBack(Items, MakeCompletionItem(V->Name, kCompletionKindVariable));
            }
            if (const auto Global = LoadSiblingGlobalSheet(UriToPath(Uri))) {
                for (const auto* V : CollectInScopeVariables(*Global)) {
                    Amanuensis::Json::PushBack(Items, MakeCompletionItem(V->Name, kCompletionKindVariable));
                }
            }
        } else if (Context.Kind == CompletionContextKind::Value) {
            for (std::string_view Keyword : PropertyValueKeywords(Context.Property)) {
                Amanuensis::Json::PushBack(Items, MakeCompletionItem(std::string(Keyword), kCompletionKindKeyword));
            }
        } else { // Statement
            for (std::string_view Name : kPrimitiveSelectorNames) {
                Amanuensis::Json::PushBack(
                    Items, MakeCompletionItem(std::string(Name), kCompletionKindClass, "primitive selector"));
            }
            for (std::string_view Name : kPseudoClassNames) {
                Amanuensis::Json::PushBack(Items,
                                            MakeCompletionItem(std::string(Name), kCompletionKindKeyword, "pseudo-class"));
            }
            if (Context.Depth > 0) {
                for (std::string_view Name : kPropertyNames) {
                    Amanuensis::Json::PushBack(Items, MakeCompletionItem(std::string(Name), kCompletionKindProperty));
                }
            }
        }
    }

    Amanuensis::JsonValue Result = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Result, "isIncomplete", Amanuensis::JsonValue(false));
    Amanuensis::Json::Insert(Result, "items", std::move(Items));
    Reply(Id, std::move(Result));
}

void Server::HandleDefinition(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params) {
    const std::string Uri =
        Amanuensis::Json::AsString(Amanuensis::Json::Get(Amanuensis::Json::Get(Params, "textDocument"), "uri"));
    const auto [Line, Column] = PositionFromParams(Amanuensis::Json::Get(Params, "position"));

    const auto DocIt = Documents_.find(Uri);
    if (DocIt == Documents_.end()) {
        Reply(Id, Amanuensis::JsonValue());
        return;
    }
    const OpenDocument& Doc = DocIt->second;

    const auto Token = TokenAtPosition(Doc.Text, Line, Column);
    if (!Token || Token->Kind != Cosmos::TokenKind::VariableName) {
        Reply(Id, Amanuensis::JsonValue());
        return;
    }

    // Search this file's own in-scope variables first, then the sibling
    // global.cosmos's (docs/cosmos_core_spec.md §1.3's cascade order, applied to
    // lookup rather than value resolution).
    for (const auto* V : CollectInScopeVariables(*Doc.Parsed.Sheet)) {
        if (V->Name == Token->Text) {
            Amanuensis::JsonValue Location = Amanuensis::Json::MakeObject();
            Amanuensis::Json::Insert(Location, "uri", Amanuensis::JsonValue(Uri));
            Amanuensis::Json::Insert(Location, "range", MakeRange(V->Location.Line, V->Location.Column));
            Reply(Id, std::move(Location));
            return;
        }
    }

    const std::string Path = UriToPath(Uri);
    if (const auto Global = LoadSiblingGlobalSheet(Path)) {
        for (const auto* V : CollectInScopeVariables(*Global)) {
            if (V->Name == Token->Text) {
                const std::filesystem::path GlobalPath = std::filesystem::path(Path).parent_path() / "global.cosmos";
                Amanuensis::JsonValue Location = Amanuensis::Json::MakeObject();
                Amanuensis::Json::Insert(Location, "uri", Amanuensis::JsonValue(PathToUri(GlobalPath.string())));
                Amanuensis::Json::Insert(Location, "range", MakeRange(V->Location.Line, V->Location.Column));
                Reply(Id, std::move(Location));
                return;
            }
        }
    }

    Reply(Id, Amanuensis::JsonValue());
}

} // namespace CosmosLsp
