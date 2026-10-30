#include "Server.h"

#include "CompletionContext.h"
#include "LspSupport.h"
#include "Navigation.h"
#include "Cosmos/Properties.h"

#include <algorithm>
#include <cctype>

namespace CosmosLsp {

namespace {

using Json = Amanuensis::Json;
using Amanuensis::JsonValue;

constexpr long long kSymbolNamespace = 3;
constexpr long long kSymbolClass     = 5;
constexpr long long kSymbolVariable  = 13;
constexpr long long kSymbolStruct    = 23;

constexpr long long kHighlightRead  = 2;
constexpr long long kHighlightWrite = 3;

JsonValue RangeOf(const TextSpan& Span) { return MakeRange(Span.Line, Span.Column, Span.EndLine, Span.EndColumn); }

JsonValue RangeOf(const Cosmos::SourceLocation& Start, const Cosmos::SourceLocation& End) {
    return MakeRange(Start.Line, Start.Column, End.Line, End.Column);
}

JsonValue MakeLocation(const std::string& Path, const TextSpan& Span) {
    JsonValue Location = Json::MakeObject();
    Json::Insert(Location, "uri", JsonValue(PathToUri(Path)));
    Json::Insert(Location, "range", RangeOf(Span));
    return Location;
}

std::string DocumentUri(const JsonValue& Params) {
    return Json::AsString(Json::Get(Json::Get(Params, "textDocument"), "uri"));
}

std::pair<std::uint32_t, std::uint32_t> CursorOf(const JsonValue& Params) {
    const JsonValue& Position = Json::Get(Params, "position");
    return {static_cast<std::uint32_t>(Json::AsInteger(Json::Get(Position, "line"))) + 1,
            static_cast<std::uint32_t>(Json::AsInteger(Json::Get(Position, "character"))) + 1};
}

// The text between two locations in Source, or "" when they don't fall inside it.
std::string Slice(std::string_view Source, const Cosmos::SourceLocation& Start, const Cosmos::SourceLocation& End) {
    auto OffsetOf = [&](const Cosmos::SourceLocation& At) -> std::optional<std::size_t> {
        std::size_t   Offset = 0;
        std::uint32_t Line   = 1;
        while (Line < At.Line) {
            const std::size_t Newline = Source.find('\n', Offset);
            if (Newline == std::string_view::npos) {
                return std::nullopt;
            }
            Offset = Newline + 1;
            ++Line;
        }
        Offset += At.Column - 1;
        return Offset <= Source.size() ? std::optional(Offset) : std::nullopt;
    };
    const auto From = OffsetOf(Start);
    const auto To   = OffsetOf(End);
    if (!From || !To || *To < *From) {
        return {};
    }
    return std::string(Source.substr(*From, *To - *From));
}

struct FoundVariable {
    const Cosmos::VariableDeclaration* Declaration{nullptr};
    std::string                        Path;
    std::string_view                   Source;
};

const Cosmos::VariableDeclaration* FindVariable(const std::vector<const Cosmos::VariableDeclaration*>& In,
                                                std::string_view Name) {
    const auto It = std::ranges::find_if(In, [&](const auto* V) { return V->Name == Name; });
    return It == In.end() ? nullptr : *It;
}

struct RuleAtPath {
    const Cosmos::Rule* R;
    std::string         Path;
    std::string         Selector;
};

void CollectRulesForClass(const std::vector<Cosmos::RulePtr>& Rules, const std::string& Ancestors,
                          std::string_view ClassName, const std::string& Path, std::vector<RuleAtPath>& Out) {
    for (const Cosmos::RulePtr& R : Rules) {
        const std::string Selector = Ancestors.empty() ? SelectorText(*R) : Ancestors + " " + SelectorText(*R);
        if (R->Kind == Cosmos::SelectorKind::Class && R->ClassName == ClassName) {
            Out.push_back(RuleAtPath{R.get(), Path, Selector});
        }
        CollectRulesForClass(R->NestedRules, Selector, ClassName, Path, Out);
    }
}

std::string FileName(const std::string& Path) { return std::filesystem::path(Path).filename().string(); }

std::string Lowercase(std::string_view Text) {
    std::string Out(Text);
    std::ranges::transform(Out, Out.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
    return Out;
}

bool SkippedFolder(const std::filesystem::path& Folder) {
    const std::string Name = Folder.filename().string();
    return Name.starts_with('.') || Name == "build" || Name.starts_with("build-") || Name.starts_with("build_") || Name == "node_modules" || Name == "libs" ||
           Name == "_deps";
}

template <typename Visit>
void ForEachFileUnder(const std::filesystem::path& Folder, std::string_view Extension, Visit&& Each) {
    std::error_code Error;
    std::filesystem::recursive_directory_iterator It(Folder, std::filesystem::directory_options::skip_permission_denied,
                                                     Error);
    for (; !Error && It != std::filesystem::recursive_directory_iterator(); It.increment(Error)) {
        if (It->is_directory(Error) && SkippedFolder(It->path())) {
            It.disable_recursion_pending();
            continue;
        }
        if (It->is_regular_file(Error) && It->path().extension() == Extension) {
            Each(It->path().string());
        }
    }
}

JsonValue MakeDocumentSymbol(const std::string& Name, long long Kind, JsonValue Range, JsonValue Selection,
                             const std::string& Detail = "") {
    JsonValue Symbol = Json::MakeObject();
    Json::Insert(Symbol, "name", JsonValue(Name));
    if (!Detail.empty()) {
        Json::Insert(Symbol, "detail", JsonValue(Detail));
    }
    Json::Insert(Symbol, "kind", JsonValue(Kind));
    Json::Insert(Symbol, "range", std::move(Range));
    Json::Insert(Symbol, "selectionRange", std::move(Selection));
    Json::Insert(Symbol, "children", Json::MakeArray());
    return Symbol;
}

JsonValue VariableSymbol(const Cosmos::VariableDeclaration& V, std::string_view Source) {
    const Cosmos::SourceLocation NameEnd{V.Location.FilePath, V.Location.Line,
                                         V.Location.Column + static_cast<std::uint32_t>(V.Name.size()) + 2};
    return MakeDocumentSymbol("--" + V.Name, kSymbolVariable, RangeOf(V.Location, V.Value.End),
                              RangeOf(V.Location, NameEnd), Slice(Source, V.Value.Location, V.Value.End));
}

JsonValue RuleSymbol(const Cosmos::Rule& R, std::string_view Source) {
    JsonValue Symbol = MakeDocumentSymbol(SelectorText(R),
                                          R.Kind == Cosmos::SelectorKind::Class ? kSymbolClass : kSymbolStruct,
                                          RangeOf(R.Location, R.End), RangeOf(R.Location, R.SelectorEnd));
    JsonValue Children = Json::MakeArray();
    for (const Cosmos::VariableDeclaration& V : R.Variables) {
        Json::PushBack(Children, VariableSymbol(V, Source));
    }
    for (const Cosmos::RulePtr& Nested : R.NestedRules) {
        Json::PushBack(Children, RuleSymbol(*Nested, Source));
    }
    Json::Insert(Symbol, "children", std::move(Children));
    return Symbol;
}

} // namespace

std::optional<std::string> Server::TextOf(const std::string& Path) const {
    if (const auto Open = Documents_.find(PathToUri(Path)); Open != Documents_.end()) {
        return Open->second.Text;
    }
    return ReadFile(Path);
}

std::vector<std::string> Server::CosmosFilesBeside(const std::string& Path) const {
    std::vector<std::string> Files;
    std::error_code          Error;
    for (const auto& Entry : std::filesystem::directory_iterator(std::filesystem::path(Path).parent_path(), Error)) {
        if (Entry.path().extension() == ".cosmos") {
            Files.push_back(Entry.path().string());
        }
    }
    for (const auto& [Uri, Doc] : Documents_) {
        const std::string OpenPath = UriToPath(Uri);
        if (IsCosmosPath(OpenPath) && std::filesystem::path(OpenPath).parent_path() == std::filesystem::path(Path).parent_path() &&
            std::ranges::find(Files, OpenPath) == Files.end()) {
            Files.push_back(OpenPath);
        }
    }
    std::ranges::sort(Files);
    return Files;
}

std::vector<std::string> Server::ChaosFilesUnder(const std::filesystem::path& Folder) const {
    std::vector<std::string> Files;
    ForEachFileUnder(Folder, ".chaos", [&](std::string File) { Files.push_back(std::move(File)); });
    std::ranges::sort(Files);
    return Files;
}

void Server::HandleHover(const JsonValue& Id, const JsonValue& Params) {
    const std::string Uri  = DocumentUri(Params);
    const auto        Doc  = Documents_.find(Uri);
    const auto [Line, Column] = CursorOf(Params);
    if (Doc == Documents_.end() || !Doc->second.Parsed.Sheet) {
        Reply(Id, JsonValue());
        return;
    }
    const std::optional<NamedSymbol> Symbol = SymbolAt(Doc->second.Text, Line, Column);
    if (!Symbol) {
        Reply(Id, JsonValue());
        return;
    }
    const std::string Path = UriToPath(Uri);
    std::string       Text;

    if (Symbol->Kind == NamedSymbolKind::Property) {
        const Cosmos::PropertyInfo* Info = Cosmos::FindProperty(Symbol->Name);
        if (!Info) {
            Reply(Id, JsonValue());
            return;
        }
        Text = "**`" + std::string(Info->Name) + "`**: " + std::string(Info->Summary) + "\n\nTakes " +
               Cosmos::DescribeValues(*Info) + ".";
        if (Info->ContainerOnly) {
            Text += "\n\nApplies only to containers (`frame`, `grid`, `scroll`, `inline`).";
        }
        if (Info->Inherited) {
            Text += "\n\nInherited.";
        }
    } else if (Symbol->Kind == NamedSymbolKind::Variable) {
        const Cosmos::Stylesheet* Global = GlobalSheetFor(Path);
        const auto                Local  = CollectInScopeVariables(*Doc->second.Parsed.Sheet);
        const auto                Shared = Global ? CollectRootVariables(*Global) : std::vector<const Cosmos::VariableDeclaration*>{};
        const std::optional<std::string> GlobalText = Global ? TextOf(GlobalPathFor(Path)) : std::nullopt;
        auto Lookup = [&](std::string_view Name) -> FoundVariable {
            const bool IsGlobal = IsGlobalPath(Path);
            if (const auto* V = FindVariable(IsGlobal ? CollectRootVariables(*Doc->second.Parsed.Sheet) : Local, Name)) {
                return FoundVariable{V, Path, Doc->second.Text};
            }
            if (const auto* V = FindVariable(Shared, Name); V && GlobalText) {
                return FoundVariable{V, GlobalPathFor(Path), *GlobalText};
            }
            return {};
        };
        const FoundVariable Declared = Lookup(Symbol->Name);
        if (!Declared.Declaration) {
            Reply(Id, JsonValue());
            return;
        }
        FoundVariable Found = Declared;
        Text = "```cosmos\n--" + Symbol->Name + ": " +
               Slice(Found.Source, Found.Declaration->Value.Location, Found.Declaration->Value.End) + ";\n```";
        for (int Hops = 0; Found.Declaration->Value.VariableRef && Hops < 4; ++Hops) {
            Found = Lookup(*Found.Declaration->Value.VariableRef);
            if (!Found.Declaration) {
                break;
            }
            Text += "\n\n= `" + Slice(Found.Source, Found.Declaration->Value.Location, Found.Declaration->Value.End) +
                    "` (`--" + Found.Declaration->Name + "`)";
        }
        Text += "\n\nDeclared in `" + FileName(Declared.Path) + "`, line " +
                std::to_string(Declared.Declaration->Location.Line) + ".";
    } else {
        std::vector<RuleAtPath> Rules;
        if (const Cosmos::Stylesheet* Global = GlobalSheetFor(Path)) {
            CollectRulesForClass(Global->Rules, "", Symbol->Name, GlobalPathFor(Path), Rules);
        }
        CollectRulesForClass(Doc->second.Parsed.Sheet->Rules, "", Symbol->Name, Path, Rules);
        Text = "**`." + Symbol->Name + "`**: " + std::to_string(Rules.size()) +
               (Rules.size() == 1 ? " rule" : " rules") + ", in cascade order:\n";
        for (const RuleAtPath& Rule : Rules) {
            Text += "\n- `" + Rule.Selector + "` " + FileName(Rule.Path) + ":" + std::to_string(Rule.R->Location.Line);
        }
    }

    JsonValue Contents = Json::MakeObject();
    Json::Insert(Contents, "kind", JsonValue("markdown"));
    Json::Insert(Contents, "value", JsonValue(Text));
    JsonValue Result = Json::MakeObject();
    Json::Insert(Result, "contents", std::move(Contents));
    Json::Insert(Result, "range", RangeOf(Symbol->Span));
    Reply(Id, std::move(Result));
}

bool Server::DeclaresVariable(const std::string& Path, std::string_view Name) const {
    if (const auto Open = Documents_.find(PathToUri(Path)); Open != Documents_.end()) {
        return Open->second.Parsed.Sheet &&
               FindVariable(CollectInScopeVariables(*Open->second.Parsed.Sheet), Name);
    }
    const auto Text = ReadFile(Path);
    if (!Text) {
        return false;
    }
    Cosmos::Parser          P(*Text, Path);
    const Cosmos::ParseResult Parsed = P.Parse();
    return Parsed.Sheet && FindVariable(CollectInScopeVariables(*Parsed.Sheet), Name);
}

std::optional<NamedSymbol> Server::SymbolAtCursor(const std::string& Path, std::uint32_t Line,
                                                  std::uint32_t Column) const {
    const auto Text = TextOf(Path);
    if (!Text) {
        return std::nullopt;
    }
    return Path.ends_with(".chaos") ? ChaosClassAt(*Text, Line, Column) : SymbolAt(*Text, Line, Column);
}

std::vector<Server::FileOccurrence> Server::OccurrencesOf(const std::string& Path, const NamedSymbol& Symbol) const {
    std::vector<FileOccurrence> Found;
    auto Add = [&](const std::string& File, const std::vector<Occurrence>& InFile, bool InChaos) {
        for (const Occurrence& Each : InFile) {
            Found.push_back(FileOccurrence{File, Each, InChaos});
        }
    };
    if (Symbol.Kind == NamedSymbolKind::Variable) {
        const bool OwnDeclares = !IsGlobalPath(Path) && DeclaresVariable(Path, Symbol.Name);
        const std::vector<std::string> Files = OwnDeclares ? std::vector<std::string>{Path} : CosmosFilesBeside(Path);
        for (const std::string& File : Files) {
            if (!OwnDeclares && !IsGlobalPath(File) && DeclaresVariable(File, Symbol.Name)) {
                continue;
            }
            if (const auto FileText = TextOf(File)) {
                Add(File, VariableOccurrences(*FileText, Symbol.Name), false);
            }
        }
    } else if (Symbol.Kind == NamedSymbolKind::Class) {
        const std::filesystem::path Folder = std::filesystem::path(Path).parent_path();
        for (const std::string& File :
             CosmosFilesBeside(Path.ends_with(".chaos") ? (Folder / "global.cosmos").string() : Path)) {
            if (const auto FileText = TextOf(File)) {
                Add(File, ClassSelectorOccurrences(*FileText, Symbol.Name), false);
            }
        }
        for (const std::string& File : ChaosFilesUnder(Folder)) {
            if (const auto FileText = TextOf(File)) {
                Add(File, ClassUsesInChaos(*FileText, Symbol.Name), true);
            }
        }
    }
    return Found;
}

void Server::HandleReferences(const JsonValue& Id, const JsonValue& Params) {
    const std::string Path = UriToPath(DocumentUri(Params));
    const auto [Line, Column] = CursorOf(Params);
    const JsonValue* IncludeDeclaration = FindPath(Params, {"context", "includeDeclaration"});
    const bool WithDeclarations = !IncludeDeclaration || !Json::IsBoolean(*IncludeDeclaration) ||
                                  Json::AsBoolean(*IncludeDeclaration);

    JsonValue Locations = Json::MakeArray();
    if (const std::optional<NamedSymbol> Symbol = SymbolAtCursor(Path, Line, Column)) {
        for (const FileOccurrence& Each : OccurrencesOf(Path, *Symbol)) {
            if (WithDeclarations || !Each.Found.IsDeclaration) {
                Json::PushBack(Locations, MakeLocation(Each.Path, Each.Found.Span));
            }
        }
    }
    Reply(Id, std::move(Locations));
}

void Server::HandleDocumentHighlight(const JsonValue& Id, const JsonValue& Params) {
    const std::string Uri = DocumentUri(Params);
    const auto [Line, Column] = CursorOf(Params);
    JsonValue  Highlights = Json::MakeArray();
    const auto Doc        = Documents_.find(Uri);
    if (Doc != Documents_.end()) {
        const std::optional<NamedSymbol> Symbol = SymbolAt(Doc->second.Text, Line, Column);
        std::vector<Occurrence>          Found;
        if (Symbol && Symbol->Kind == NamedSymbolKind::Variable) {
            Found = VariableOccurrences(Doc->second.Text, Symbol->Name);
        } else if (Symbol && Symbol->Kind == NamedSymbolKind::Class) {
            Found = ClassSelectorOccurrences(Doc->second.Text, Symbol->Name);
        }
        for (const Occurrence& Each : Found) {
            JsonValue Highlight = Json::MakeObject();
            Json::Insert(Highlight, "range", RangeOf(Each.Span));
            Json::Insert(Highlight, "kind", JsonValue(Each.IsDeclaration ? kHighlightWrite : kHighlightRead));
            Json::PushBack(Highlights, std::move(Highlight));
        }
    }
    Reply(Id, std::move(Highlights));
}

void Server::HandleDocumentSymbol(const JsonValue& Id, const JsonValue& Params) {
    JsonValue  Symbols = Json::MakeArray();
    const auto Doc     = Documents_.find(DocumentUri(Params));
    if (Doc != Documents_.end() && Doc->second.Parsed.Sheet) {
        const Cosmos::Stylesheet& Sheet = *Doc->second.Parsed.Sheet;
        if (Sheet.Root) {
            const Cosmos::SourceLocation RootEnd{"", Sheet.Root->Location.Line, Sheet.Root->Location.Column + 5};
            JsonValue Root = MakeDocumentSymbol(":root", kSymbolNamespace, RangeOf(Sheet.Root->Location, Sheet.Root->End),
                                                RangeOf(Sheet.Root->Location, RootEnd));
            JsonValue Children = Json::MakeArray();
            for (const Cosmos::VariableDeclaration& V : Sheet.Root->Variables) {
                Json::PushBack(Children, VariableSymbol(V, Doc->second.Text));
            }
            Json::Insert(Root, "children", std::move(Children));
            Json::PushBack(Symbols, std::move(Root));
        }
        for (const Cosmos::RulePtr& R : Sheet.Rules) {
            Json::PushBack(Symbols, RuleSymbol(*R, Doc->second.Text));
        }
    }
    Reply(Id, std::move(Symbols));
}

void Server::HandleWorkspaceSymbol(const JsonValue& Id, const JsonValue& Params) {
    const JsonValue*  QueryValue = FindPath(Params, {"query"});
    const std::string Query      = QueryValue && Json::IsString(*QueryValue) ? Lowercase(Json::AsString(*QueryValue)) : "";

    std::vector<std::string> Files;
    auto AddFolder = [&](const std::filesystem::path& Folder) {
        ForEachFileUnder(Folder, ".cosmos", [&](std::string File) {
            if (std::ranges::find(Files, File) == Files.end()) {
                Files.push_back(std::move(File));
            }
        });
    };
    if (!RootPath_.empty()) {
        AddFolder(RootPath_);
    }
    for (const auto& [Uri, Doc] : Documents_) {
        const std::string OpenPath = UriToPath(Uri);
        if (IsCosmosPath(OpenPath) && (RootPath_.empty() || !OpenPath.starts_with(RootPath_))) {
            AddFolder(std::filesystem::path(OpenPath).parent_path());
            if (std::ranges::find(Files, OpenPath) == Files.end()) {
                Files.push_back(OpenPath);
            }
        }
    }
    std::ranges::sort(Files);

    JsonValue Symbols = Json::MakeArray();
    auto      Add     = [&](const std::string& Name, long long Kind, const std::string& File, const TextSpan& Span) {
        if (!Query.empty() && Lowercase(Name).find(Query) == std::string::npos) {
            return;
        }
        JsonValue Symbol = Json::MakeObject();
        Json::Insert(Symbol, "name", JsonValue(Name));
        Json::Insert(Symbol, "kind", JsonValue(Kind));
        Json::Insert(Symbol, "location", MakeLocation(File, Span));
        Json::Insert(Symbol, "containerName", JsonValue(FileName(File)));
        Json::PushBack(Symbols, std::move(Symbol));
    };
    for (const std::string& File : Files) {
        const auto Text = TextOf(File);
        if (!Text) {
            continue;
        }
        std::vector<std::string> SeenClasses;
        const std::vector<Cosmos::Token> Tokens = TokenizeDocument(*Text);
        for (std::size_t I = 0; I < Tokens.size(); ++I) {
            const Cosmos::Token& T = Tokens[I];
            const TextSpan       Span{T.Location.Line, T.Location.Column, T.End.Line, T.End.Column};
            if (T.Kind == Cosmos::TokenKind::ClassName &&
                std::ranges::find(SeenClasses, T.Text) == SeenClasses.end()) {
                SeenClasses.push_back(T.Text);
                Add("." + T.Text, kSymbolClass, File, Span);
            } else if (T.Kind == Cosmos::TokenKind::VariableName && I + 1 < Tokens.size() &&
                       Tokens[I + 1].Kind == Cosmos::TokenKind::Colon) {
                Add("--" + T.Text, kSymbolVariable, File, Span);
            }
        }
    }
    Reply(Id, std::move(Symbols));
}

} // namespace CosmosLsp
