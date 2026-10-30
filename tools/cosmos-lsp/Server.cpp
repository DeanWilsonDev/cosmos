#include "Server.h"

#include "CompletionContext.h"
#include "JsonRpc.h"
#include "LspSupport.h"
#include "Cosmos/Checker.h"
#include "Cosmos/Parser.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace CosmosLsp {

namespace {

// The range a diagnostic underlines: Start..End when the diagnostic has a span, else the
// token that starts at Start, else one column.
Amanuensis::JsonValue DiagnosticRange(const std::vector<Cosmos::Token>& Tokens, const Cosmos::SourceLocation& Start,
                                      const Cosmos::SourceLocation& End) {
    if (End.Line > Start.Line || (End.Line == Start.Line && End.Column > Start.Column)) {
        return MakeRange(Start.Line, Start.Column, End.Line, End.Column);
    }
    for (const Cosmos::Token& T : Tokens) {
        if (T.Kind != Cosmos::TokenKind::EndOfFile && T.Location.Line == Start.Line &&
            T.Location.Column == Start.Column) {
            return MakeRange(Start.Line, Start.Column, T.End.Line, T.End.Column);
        }
    }
    return MakeRange(Start.Line, Start.Column, Start.Line, Start.Column + 1);
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

// The requests a .chaos buffer gets answers to: the ones about its class strings. Every
// other request about a file that isn't .cosmos gets null, so a client that attaches
// cosmos-lsp to .chaos buffers for class references sees nothing else from it there.
bool AnswersOutsideCosmos(std::string_view Method) {
    return Method == "textDocument/references" || Method == "textDocument/prepareRename" ||
           Method == "textDocument/rename";
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

void Server::ReplyError(const Amanuensis::JsonValue& Id, long long Code, const std::string& Message) {
    Amanuensis::JsonValue Error = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Error, "code", Amanuensis::JsonValue(Code));
    Amanuensis::Json::Insert(Error, "message", Amanuensis::JsonValue(Message));
    Amanuensis::JsonValue Response = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Response, "jsonrpc", Amanuensis::JsonValue("2.0"));
    Amanuensis::Json::Insert(Response, "id", Id);
    Amanuensis::Json::Insert(Response, "error", std::move(Error));
    JsonRpc::WriteMessage(Out_, Response);
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

    if (IsRequest && Method.starts_with("textDocument/") && !AnswersOutsideCosmos(Method)) {
        const Amanuensis::JsonValue* Uri = FindPath(Params, {"textDocument", "uri"});
        if (Uri && Amanuensis::Json::IsString(*Uri) && !IsCosmosPath(UriToPath(Amanuensis::Json::AsString(*Uri)))) {
            Reply(Id, Amanuensis::JsonValue());
            return;
        }
    }

    if (Method == "initialize") {
        HandleInitialize(Id, Params);
    } else if (Method == "initialized") {
        HandleInitialized();
    } else if (Method == "$/setTrace" || Method == "workspace/didChangeConfiguration") {
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
    } else if (Method == "textDocument/didSave") {
        HandleDidSave(Params);
    } else if (Method == "workspace/didChangeWatchedFiles") {
        HandleDidChangeWatchedFiles(Params);
    } else if (Method == "textDocument/completion") {
        HandleCompletion(Id, Params);
    } else if (Method == "textDocument/definition") {
        HandleDefinition(Id, Params);
    } else if (Method == "textDocument/hover") {
        HandleHover(Id, Params);
    } else if (Method == "textDocument/references") {
        HandleReferences(Id, Params);
    } else if (Method == "textDocument/documentHighlight") {
        HandleDocumentHighlight(Id, Params);
    } else if (Method == "textDocument/documentSymbol") {
        HandleDocumentSymbol(Id, Params);
    } else if (Method == "workspace/symbol") {
        HandleWorkspaceSymbol(Id, Params);
    } else if (Method == "textDocument/prepareRename") {
        HandlePrepareRename(Id, Params);
    } else if (Method == "textDocument/rename") {
        HandleRename(Id, Params);
    } else if (Method == "textDocument/codeAction") {
        HandleCodeAction(Id, Params);
    } else if (Method == "textDocument/documentColor") {
        HandleDocumentColor(Id, Params);
    } else if (Method == "textDocument/colorPresentation") {
        HandleColorPresentation(Id, Params);
    } else if (Method == "textDocument/foldingRange") {
        HandleFoldingRange(Id, Params);
    } else if (Method == "textDocument/formatting") {
        HandleFormatting(Id, Params, false);
    } else if (Method == "textDocument/rangeFormatting") {
        HandleFormatting(Id, Params, true);
    } else if (IsRequest) {
        ReplyError(Id, -32601, "method not found: " + Method);
    }
    // An unknown notification (no id) is silently ignored, per LSP's own "must not
    // fail" guidance for messages a server doesn't recognise.
}

void Server::HandleInitialize(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params) {
    const Amanuensis::JsonValue* Watch =
        FindPath(Params, {"capabilities", "workspace", "didChangeWatchedFiles", "dynamicRegistration"});
    WatchGlobalFiles_ = Watch && Amanuensis::Json::IsBoolean(*Watch) && Amanuensis::Json::AsBoolean(*Watch);
    if (const Amanuensis::JsonValue* Root = FindPath(Params, {"rootUri"}); Root && Amanuensis::Json::IsString(*Root)) {
        RootPath_ = UriToPath(Amanuensis::Json::AsString(*Root));
    }

    Amanuensis::JsonValue Completion = Amanuensis::Json::MakeObject();
    Amanuensis::JsonValue TriggerChars = Amanuensis::Json::MakeArray();
    Amanuensis::Json::PushBack(TriggerChars, Amanuensis::JsonValue(":"));
    Amanuensis::Json::PushBack(TriggerChars, Amanuensis::JsonValue("("));
    Amanuensis::Json::PushBack(TriggerChars, Amanuensis::JsonValue(" "));
    Amanuensis::Json::PushBack(TriggerChars, Amanuensis::JsonValue("."));
    Amanuensis::Json::Insert(Completion, "triggerCharacters", std::move(TriggerChars));

    Amanuensis::JsonValue Capabilities = Amanuensis::Json::MakeObject();
    Amanuensis::JsonValue Sync = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Sync, "openClose", Amanuensis::JsonValue(true));
    Amanuensis::Json::Insert(Sync, "change", Amanuensis::JsonValue(static_cast<long long>(1)));
    Amanuensis::Json::Insert(Sync, "save", Amanuensis::JsonValue(true));
    Amanuensis::Json::Insert(Capabilities, "textDocumentSync", std::move(Sync));
    Amanuensis::Json::Insert(Capabilities, "completionProvider", std::move(Completion));
    Amanuensis::Json::Insert(Capabilities, "definitionProvider", Amanuensis::JsonValue(true));
    for (const char* Provider : {"hoverProvider", "referencesProvider", "documentHighlightProvider",
                                 "documentSymbolProvider", "workspaceSymbolProvider", "colorProvider",
                                 "foldingRangeProvider", "documentFormattingProvider",
                                 "documentRangeFormattingProvider"}) {
        Amanuensis::Json::Insert(Capabilities, Provider, Amanuensis::JsonValue(true));
    }
    Amanuensis::JsonValue Rename = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Rename, "prepareProvider", Amanuensis::JsonValue(true));
    Amanuensis::Json::Insert(Capabilities, "renameProvider", std::move(Rename));
    Amanuensis::JsonValue CodeActionKinds = Amanuensis::Json::MakeArray();
    Amanuensis::Json::PushBack(CodeActionKinds, Amanuensis::JsonValue("quickfix"));
    Amanuensis::JsonValue CodeAction = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(CodeAction, "codeActionKinds", std::move(CodeActionKinds));
    Amanuensis::Json::Insert(Capabilities, "codeActionProvider", std::move(CodeAction));

    Amanuensis::JsonValue ServerInfo = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(ServerInfo, "name", Amanuensis::JsonValue("cosmos-lsp"));
    Amanuensis::Json::Insert(ServerInfo, "version", Amanuensis::JsonValue("0.1.0"));

    Amanuensis::JsonValue Result = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Result, "capabilities", std::move(Capabilities));
    Amanuensis::Json::Insert(Result, "serverInfo", std::move(ServerInfo));
    Reply(Id, std::move(Result));
}

void Server::HandleInitialized() {
    if (!WatchGlobalFiles_) {
        return;
    }
    Amanuensis::JsonValue Watcher = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Watcher, "globPattern", Amanuensis::JsonValue("**/global.cosmos"));
    Amanuensis::JsonValue Watchers = Amanuensis::Json::MakeArray();
    Amanuensis::Json::PushBack(Watchers, std::move(Watcher));
    Amanuensis::JsonValue Options = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Options, "watchers", std::move(Watchers));
    Amanuensis::JsonValue Registration = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Registration, "id", Amanuensis::JsonValue("cosmos-global-watch"));
    Amanuensis::Json::Insert(Registration, "method", Amanuensis::JsonValue("workspace/didChangeWatchedFiles"));
    Amanuensis::Json::Insert(Registration, "registerOptions", std::move(Options));
    Amanuensis::JsonValue Registrations = Amanuensis::Json::MakeArray();
    Amanuensis::Json::PushBack(Registrations, std::move(Registration));
    Amanuensis::JsonValue Params = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Params, "registrations", std::move(Registrations));

    Amanuensis::JsonValue Request = Amanuensis::Json::MakeObject();
    Amanuensis::Json::Insert(Request, "jsonrpc", Amanuensis::JsonValue("2.0"));
    Amanuensis::Json::Insert(Request, "id", Amanuensis::JsonValue("cosmos-lsp-watch-global"));
    Amanuensis::Json::Insert(Request, "method", Amanuensis::JsonValue("client/registerCapability"));
    Amanuensis::Json::Insert(Request, "params", std::move(Params));
    JsonRpc::WriteMessage(Out_, Request);
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
    const std::string Uri =
        Amanuensis::Json::AsString(Amanuensis::Json::Get(Amanuensis::Json::Get(Params, "textDocument"), "uri"));
    Documents_.erase(Uri);
    const std::string Path = UriToPath(Uri);
    if (IsGlobalPath(Path)) {
        GlobalChanged(Path);
    }
}

void Server::HandleDidSave(const Amanuensis::JsonValue& Params) {
    const std::string Path = UriToPath(
        Amanuensis::Json::AsString(Amanuensis::Json::Get(Amanuensis::Json::Get(Params, "textDocument"), "uri")));
    if (IsGlobalPath(Path)) {
        GlobalChanged(Path);
    }
}

void Server::HandleDidChangeWatchedFiles(const Amanuensis::JsonValue& Params) {
    const Amanuensis::JsonValue* Changes = FindPath(Params, {"changes"});
    if (!Changes || !Amanuensis::Json::IsArray(*Changes)) {
        return;
    }
    for (std::size_t I = 0; I < Amanuensis::Json::Size(*Changes); ++I) {
        const Amanuensis::JsonValue* Uri = FindPath(Amanuensis::Json::At(*Changes, I), {"uri"});
        if (!Uri || !Amanuensis::Json::IsString(*Uri)) {
            continue;
        }
        const std::string Path = UriToPath(Amanuensis::Json::AsString(*Uri));
        if (IsGlobalPath(Path)) {
            GlobalChanged(Path);
        }
    }
}

std::string Server::GlobalPathFor(const std::string& Path) const {
    return (std::filesystem::path(Path).parent_path() / "global.cosmos").string();
}

const Cosmos::Stylesheet* Server::GlobalSheetFor(const std::string& Path) {
    if (IsGlobalPath(Path)) {
        return nullptr;
    }
    const std::string GlobalPath = GlobalPathFor(Path);
    if (const auto Open = Documents_.find(PathToUri(GlobalPath)); Open != Documents_.end()) {
        return Open->second.Parsed.Sheet ? &*Open->second.Parsed.Sheet : nullptr;
    }
    std::error_code Error;
    const auto      Written = std::filesystem::last_write_time(GlobalPath, Error);
    const std::optional<std::filesystem::file_time_type> Stamp =
        Error ? std::nullopt : std::optional<std::filesystem::file_time_type>(Written);
    auto Cached = DiskGlobals_.find(GlobalPath);
    if (Cached == DiskGlobals_.end() || Cached->second.Stamp != Stamp) {
        DiskSheet Sheet{{}, Stamp};
        if (const auto Text = ReadFile(GlobalPath)) {
            Cosmos::Parser P(*Text, GlobalPath);
            Sheet.Parsed = P.Parse();
        }
        Cached = DiskGlobals_.insert_or_assign(GlobalPath, std::move(Sheet)).first;
    }
    return Cached->second.Parsed.Sheet ? &*Cached->second.Parsed.Sheet : nullptr;
}

void Server::GlobalChanged(const std::string& GlobalPath) {
    DiskGlobals_.erase(GlobalPath);
    const std::filesystem::path Folder = std::filesystem::path(GlobalPath).parent_path();
    for (const auto& [Uri, Doc] : Documents_) {
        const std::string Path = UriToPath(Uri);
        if (IsCosmosPath(Path) && !IsGlobalPath(Path) && std::filesystem::path(Path).parent_path() == Folder) {
            PublishDiagnostics(Uri, Doc);
        }
    }
}

void Server::RebuildDocument(const std::string& Uri, std::string Text) {
    const std::string Path = UriToPath(Uri);

    OpenDocument& Doc = Documents_[Uri];
    Doc.Text = std::move(Text);
    if (!IsCosmosPath(Path)) {
        return;
    }

    Cosmos::Parser P(Doc.Text, Path);
    Doc.Parsed = P.Parse();

    PublishDiagnostics(Uri, Doc);
    if (IsGlobalPath(Path)) {
        GlobalChanged(Path);
    }
}

std::vector<Cosmos::CheckDiagnostic> Server::CheckDocument(const std::string& Path, const OpenDocument& Doc) {
    if (!Doc.Parsed.Sheet) {
        return {};
    }
    Cosmos::StylesheetSet Sheets;
    if (IsGlobalPath(Path)) {
        Sheets.Global = &*Doc.Parsed.Sheet;
    } else {
        Sheets.Global    = GlobalSheetFor(Path);
        Sheets.Component = &*Doc.Parsed.Sheet;
    }
    std::vector<Cosmos::CheckDiagnostic> Found = Cosmos::CheckStylesheet(Sheets);
    std::erase_if(Found, [&](const Cosmos::CheckDiagnostic& Each) {
        return !Each.Location.FilePath.empty() && Each.Location.FilePath != Path;
    });
    return Found;
}

void Server::PublishDiagnostics(const std::string& Uri, const OpenDocument& Doc) {
    const std::vector<Cosmos::Token> Tokens = TokenizeDocument(Doc.Text);
    Amanuensis::JsonValue            Diagnostics = Amanuensis::Json::MakeArray();
    for (const Cosmos::ParseError& Err : Doc.Parsed.Errors) {
        Amanuensis::Json::PushBack(Diagnostics,
                                   MakeDiagnostic(DiagnosticRange(Tokens, Err.Location, Err.Location), 1, Err.Message));
    }
    for (const Cosmos::CheckDiagnostic& Found : CheckDocument(UriToPath(Uri), Doc)) {
        const long long Severity = Found.Severity == Cosmos::CheckSeverity::Error ? 1 : 2;
        Amanuensis::Json::PushBack(
            Diagnostics, MakeDiagnostic(DiagnosticRange(Tokens, Found.Location, Found.End), Severity, Found.Message));
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
            if (const Cosmos::Stylesheet* Global = GlobalSheetFor(UriToPath(Uri))) {
                for (const auto* V : CollectRootVariables(*Global)) {
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
    if (const Cosmos::Stylesheet* Global = GlobalSheetFor(Path)) {
        for (const auto* V : CollectRootVariables(*Global)) {
            if (V->Name == Token->Text) {
                const std::filesystem::path GlobalPath = GlobalPathFor(Path);
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
