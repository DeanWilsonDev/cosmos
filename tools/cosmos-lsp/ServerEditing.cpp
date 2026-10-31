#include "Server.h"

#include "CompletionContext.h"
#include "LspSupport.h"
#include "Navigation.h"
#include "cosmos/Resolver.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <map>

namespace CosmosLsp {

namespace {

using Json = Amanuensis::Json;
using Amanuensis::JsonValue;

constexpr long long kRequestFailed = -32803;

struct LineColumn {
    std::uint32_t Line;
    std::uint32_t Column;

    friend auto operator<=>(const LineColumn&, const LineColumn&) = default;
};

JsonValue RangeOf(const TextSpan& Span) { return MakeRange(Span.Line, Span.Column, Span.EndLine, Span.EndColumn); }

JsonValue RangeOf(const Cosmos::SourceLocation& Start, const Cosmos::SourceLocation& End) {
    return MakeRange(Start.Line, Start.Column, End.Line, End.Column);
}

std::string DocumentUri(const JsonValue& Params) {
    return Json::AsString(Json::Get(Json::Get(Params, "textDocument"), "uri"));
}

LineColumn PositionAt(const JsonValue& Position) {
    return {static_cast<std::uint32_t>(Json::AsInteger(Json::Get(Position, "line"))) + 1,
            static_cast<std::uint32_t>(Json::AsInteger(Json::Get(Position, "character"))) + 1};
}

bool IsNameChar(char C) { return std::isalnum(static_cast<unsigned char>(C)) || C == '_' || C == '-'; }

// The bare name a rename asks for, with the `--` or `.` the user may have kept from the
// placeholder taken off; empty when what's left isn't a name Cosmos can write there.
std::string BareNewName(std::string_view Requested, NamedSymbolKind Kind) {
    const std::string_view Prefix = Kind == NamedSymbolKind::Variable ? "--" : ".";
    if (Requested.starts_with(Prefix)) {
        Requested.remove_prefix(Prefix.size());
    }
    if (Requested.empty() || !std::ranges::all_of(Requested, IsNameChar)) {
        return "";
    }
    if (Kind == NamedSymbolKind::Class && !std::isalpha(static_cast<unsigned char>(Requested.front())) &&
        Requested.front() != '_') {
        return "";
    }
    return std::string(Requested);
}

std::string Written(const NamedSymbol& Symbol, bool InChaos) {
    if (InChaos) {
        return Symbol.Name;
    }
    return (Symbol.Kind == NamedSymbolKind::Variable ? "--" : ".") + Symbol.Name;
}

JsonValue MakeTextEdit(JsonValue Range, const std::string& NewText) {
    JsonValue Edit = Json::MakeObject();
    Json::Insert(Edit, "range", std::move(Range));
    Json::Insert(Edit, "newText", JsonValue(NewText));
    return Edit;
}

template <typename Visit>
void ForEachRule(const std::vector<Cosmos::RulePtr>& Rules, Visit&& Each) {
    for (const Cosmos::RulePtr& R : Rules) {
        Each(*R);
        ForEachRule(R->NestedRules, Each);
    }
}

template <typename Visit>
void ForEachValue(const Cosmos::Stylesheet& Sheet, Visit&& Each) {
    if (Sheet.Root) {
        for (const Cosmos::VariableDeclaration& V : Sheet.Root->Variables) {
            Each(V.Value);
        }
    }
    ForEachRule(Sheet.Rules, [&](const Cosmos::Rule& R) {
        for (const Cosmos::VariableDeclaration& V : R.Variables) {
            Each(V.Value);
        }
        for (const auto* Declarations : {&R.Declarations, &R.StartingDeclarations}) {
            for (const Cosmos::Declaration& D : *Declarations) {
                for (const Cosmos::ValuePart& Part : D.Values) {
                    Each(Part);
                }
            }
        }
    });
}

JsonValue Channel(std::uint8_t Value) { return JsonValue(static_cast<double>(Value) / 255.0); }

std::uint8_t ChannelFrom(const JsonValue& Color, const char* Name) {
    const JsonValue& Value = Json::Get(Color, Name);
    const double     Unit  = Json::IsInteger(Value) ? static_cast<double>(Json::AsInteger(Value)) : Json::AsDouble(Value);
    return static_cast<std::uint8_t>(std::lround(std::clamp(Unit, 0.0, 1.0) * 255.0));
}

JsonValue MakeColorPresentation(const std::string& Label, const JsonValue& Range) {
    JsonValue Presentation = Json::MakeObject();
    Json::Insert(Presentation, "label", JsonValue(Label));
    Json::Insert(Presentation, "textEdit", MakeTextEdit(Range, Label));
    return Presentation;
}

void AddFold(JsonValue& Folds, const Cosmos::SourceLocation& Start, const Cosmos::SourceLocation& End) {
    if (End.Line <= Start.Line + 1) {
        return;
    }
    JsonValue Fold = Json::MakeObject();
    Json::Insert(Fold, "startLine", JsonValue(static_cast<long long>(Start.Line) - 1));
    Json::Insert(Fold, "endLine", JsonValue(static_cast<long long>(End.Line) - 2));
    Json::PushBack(Folds, std::move(Fold));
}

} // namespace

std::optional<Server::FileOccurrence> Server::FirstUseOf(const std::string& Path, const NamedSymbol& Symbol,
                                                         const std::string& NewName) const {
    if (Symbol.Kind == NamedSymbolKind::Class) {
        const std::vector<FileOccurrence> Found =
            OccurrencesOf(Path, NamedSymbol{NamedSymbolKind::Class, NewName, Symbol.Span, false});
        return Found.empty() ? std::nullopt : std::optional<FileOccurrence>(Found.front());
    }
    const bool OwnDeclares = !IsGlobalPath(Path) && DeclaresVariable(Path, Symbol.Name);
    for (const std::string& File : OwnDeclares ? std::vector<std::string>{Path} : CosmosFilesBeside(Path)) {
        if (const auto Text = TextOf(File)) {
            if (const std::vector<Occurrence> Found = VariableOccurrences(*Text, NewName); !Found.empty()) {
                return FileOccurrence{File, Found.front(), false};
            }
        }
    }
    return std::nullopt;
}

void Server::HandlePrepareRename(const JsonValue& Id, const JsonValue& Params) {
    const std::string Path         = UriToPath(DocumentUri(Params));
    const auto [Line, Column]      = PositionAt(Json::Get(Params, "position"));
    const std::optional<NamedSymbol> Symbol = SymbolAtCursor(Path, Line, Column);
    if (!Symbol || Symbol->Kind == NamedSymbolKind::Property) {
        Reply(Id, JsonValue());
        return;
    }
    JsonValue Result = Json::MakeObject();
    Json::Insert(Result, "range", RangeOf(Symbol->Span));
    Json::Insert(Result, "placeholder", JsonValue(Written(*Symbol, !IsCosmosPath(Path))));
    Reply(Id, std::move(Result));
}

void Server::HandleRename(const JsonValue& Id, const JsonValue& Params) {
    const std::string Path    = UriToPath(DocumentUri(Params));
    const auto [Line, Column] = PositionAt(Json::Get(Params, "position"));
    const std::optional<NamedSymbol> Symbol = SymbolAtCursor(Path, Line, Column);
    if (!Symbol || Symbol->Kind == NamedSymbolKind::Property) {
        ReplyError(Id, kRequestFailed, "Only a variable or a class can be renamed.");
        return;
    }
    const std::string Requested = Json::AsString(Json::Get(Params, "newName"));
    const std::string NewName   = BareNewName(Requested, Symbol->Kind);
    const bool        IsVariable = Symbol->Kind == NamedSymbolKind::Variable;
    if (NewName.empty()) {
        ReplyError(Id, kRequestFailed,
                   "`" + Requested + "` isn't a " + (IsVariable ? "variable" : "class") + " name.");
        return;
    }
    if (IsVariable && !DeclaresVariable(Path, Symbol->Name) &&
        (IsGlobalPath(Path) || !DeclaresVariable(GlobalPathFor(Path), Symbol->Name))) {
        ReplyError(Id, kRequestFailed,
                   "`--" + Symbol->Name + "` isn't declared in this file or in global.cosmos, so there's nothing to "
                   "rename.");
        return;
    }
    if (NewName == Symbol->Name) {
        Reply(Id, JsonValue());
        return;
    }

    if (const std::optional<FileOccurrence> Clash = FirstUseOf(Path, *Symbol, NewName)) {
        ReplyError(Id, kRequestFailed,
                   "`" + Written(NamedSymbol{Symbol->Kind, NewName, {}, false}, false) + "` is already " +
                       (IsVariable ? "used" : "a class") + " in " +
                       std::filesystem::path(Clash->Path).filename().string() + ", line " +
                       std::to_string(Clash->Found.Span.Line) + ".");
        return;
    }

    std::map<std::string, JsonValue> EditsByUri;
    for (const FileOccurrence& Each : OccurrencesOf(Path, *Symbol)) {
        const std::string NewText = Each.InChaos ? NewName : (IsVariable ? "--" : ".") + NewName;
        auto [It, Inserted]       = EditsByUri.try_emplace(PathToUri(Each.Path), Json::MakeArray());
        Json::PushBack(It->second, MakeTextEdit(RangeOf(Each.Found.Span), NewText));
    }
    JsonValue Changes = Json::MakeObject();
    for (auto& [Uri, Edits] : EditsByUri) {
        Json::Insert(Changes, Uri, std::move(Edits));
    }
    JsonValue Result = Json::MakeObject();
    Json::Insert(Result, "changes", std::move(Changes));
    Reply(Id, std::move(Result));
}

void Server::HandleCodeAction(const JsonValue& Id, const JsonValue& Params) {
    const std::string Uri  = DocumentUri(Params);
    const std::string Path = UriToPath(Uri);
    JsonValue         Actions = Json::MakeArray();
    const auto        Doc     = Documents_.find(Uri);
    if (Doc == Documents_.end()) {
        Reply(Id, std::move(Actions));
        return;
    }
    const JsonValue& Range = Json::Get(Params, "range");
    const LineColumn From  = PositionAt(Json::Get(Range, "start"));
    const LineColumn To    = PositionAt(Json::Get(Range, "end"));

    for (const Cosmos::CheckDiagnostic& Found : CheckDocument(Path, Doc->second)) {
        const LineColumn Start{Found.Location.Line, Found.Location.Column};
        const LineColumn End{Found.End.Line, Found.End.Column};
        if (Found.Suggestion.empty() || !(Start < End) || End < From || To < Start) {
            continue;
        }
        const long long Severity = Found.Severity == Cosmos::CheckSeverity::Error ? 1 : 2;
        JsonValue Diagnostics = Json::MakeArray();
        Json::PushBack(Diagnostics, MakeDiagnostic(RangeOf(Found.Location, Found.End), Severity, Found.Message));

        JsonValue Edits = Json::MakeArray();
        Json::PushBack(Edits, MakeTextEdit(RangeOf(Found.Location, Found.End), Found.Suggestion));
        JsonValue Changes = Json::MakeObject();
        Json::Insert(Changes, Uri, std::move(Edits));
        JsonValue Edit = Json::MakeObject();
        Json::Insert(Edit, "changes", std::move(Changes));

        JsonValue Action = Json::MakeObject();
        Json::Insert(Action, "title", JsonValue("Change to `" + Found.Suggestion + "`"));
        Json::Insert(Action, "kind", JsonValue("quickfix"));
        Json::Insert(Action, "diagnostics", std::move(Diagnostics));
        Json::Insert(Action, "isPreferred", JsonValue(true));
        Json::Insert(Action, "edit", std::move(Edit));
        Json::PushBack(Actions, std::move(Action));
    }
    Reply(Id, std::move(Actions));
}

void Server::HandleDocumentColor(const JsonValue& Id, const JsonValue& Params) {
    JsonValue  Colors = Json::MakeArray();
    const auto Doc    = Documents_.find(DocumentUri(Params));
    if (Doc != Documents_.end() && Doc->second.Parsed.Sheet) {
        ForEachValue(*Doc->second.Parsed.Sheet, [&](const Cosmos::ValuePart& Part) {
            const std::optional<Cosmos::Color> Found = Cosmos::ColorOfValue(Part);
            if (!Found || Part.Location.Line == 0) {
                return;
            }
            JsonValue Color = Json::MakeObject();
            Json::Insert(Color, "red", Channel(Found->R));
            Json::Insert(Color, "green", Channel(Found->G));
            Json::Insert(Color, "blue", Channel(Found->B));
            Json::Insert(Color, "alpha", Channel(Found->A));
            JsonValue Information = Json::MakeObject();
            Json::Insert(Information, "range", RangeOf(Part.Location, Part.End));
            Json::Insert(Information, "color", std::move(Color));
            Json::PushBack(Colors, std::move(Information));
        });
    }
    Reply(Id, std::move(Colors));
}

void Server::HandleColorPresentation(const JsonValue& Id, const JsonValue& Params) {
    const JsonValue&   Color = Json::Get(Params, "color");
    const JsonValue&   Range = Json::Get(Params, "range");
    const std::uint8_t R     = ChannelFrom(Color, "red");
    const std::uint8_t G     = ChannelFrom(Color, "green");
    const std::uint8_t B     = ChannelFrom(Color, "blue");
    const std::uint8_t A     = ChannelFrom(Color, "alpha");

    JsonValue Presentations = Json::MakeArray();
    if (A == 255) {
        Json::PushBack(Presentations, MakeColorPresentation(std::format("#{:02x}{:02x}{:02x}", R, G, B), Range));
        Json::PushBack(Presentations, MakeColorPresentation(std::format("rgb({}, {}, {})", R, G, B), Range));
    } else {
        Json::PushBack(Presentations,
                       MakeColorPresentation(std::format("#{:02x}{:02x}{:02x}{:02x}", R, G, B, A), Range));
        Json::PushBack(Presentations, MakeColorPresentation(std::format("rgba({}, {}, {}, {})", R, G, B, A), Range));
    }
    Reply(Id, std::move(Presentations));
}

void Server::HandleFoldingRange(const JsonValue& Id, const JsonValue& Params) {
    JsonValue  Folds = Json::MakeArray();
    const auto Doc   = Documents_.find(DocumentUri(Params));
    if (Doc != Documents_.end() && Doc->second.Parsed.Sheet) {
        const Cosmos::Stylesheet& Sheet = *Doc->second.Parsed.Sheet;
        if (Sheet.Root) {
            AddFold(Folds, Sheet.Root->Location, Sheet.Root->End);
        }
        ForEachRule(Sheet.Rules, [&](const Cosmos::Rule& R) { AddFold(Folds, R.Location, R.End); });
    }
    Reply(Id, std::move(Folds));
}

} // namespace CosmosLsp
