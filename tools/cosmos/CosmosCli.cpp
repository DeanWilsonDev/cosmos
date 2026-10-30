#include "CosmosCli.h"

#include "AstJson.h"
#include "CommandSupport.h"
#include "ElementPath.h"
#include "Cosmos/Checker.h"
#include "Cosmos/Explain.h"
#include "Cosmos/Parser.h"
#include "Cosmos/Tokenizer.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>

namespace CosmosCli {

namespace {

constexpr const char* kUsage =
    "usage: cosmos <command> [arguments]\n"
    "\n"
    "commands:\n"
    "  check <file or directory>...     check stylesheets, each with the global.cosmos beside it\n"
    "  fmt [--check] <file or dir>...   format stylesheets in place; --check lists files that would change\n"
    "  tokens <file>                    print the file's tokens\n"
    "  ast [--locations] <file>         print the file's syntax tree as JSON\n"
    "  explain [--root N] [--global PATH] <file> <element path>\n"
    "                                   show the rules that style an element and the style it gets,\n"
    "                                   for a path like 'Frame[class=\"card\"] > Text[class=\"title\"]:hover'\n"
    "  help                             print this message\n";

} // namespace

int UsageError(const std::string& Message, std::ostream& Err) {
    Err << "error: " << Message << "\n";
    Err << "run 'cosmos help' for usage\n";
    return kExitUsage;
}

namespace {

std::optional<std::string> ReadFile(const std::filesystem::path& Path) {
    std::ifstream Stream(Path, std::ios::binary);
    if (!Stream) {
        return std::nullopt;
    }
    std::ostringstream Contents;
    Contents << Stream.rdbuf();
    return Contents.str();
}

struct ParsedFile {
    std::string         Path;
    std::string         Text;
    Cosmos::ParseResult Parsed;
};

std::unique_ptr<ParsedFile> ParseFile(const std::string& Path) {
    auto File = std::make_unique<ParsedFile>();
    File->Path = Path;
    const std::optional<std::string> Text = ReadFile(Path);
    if (!Text) {
        return nullptr;
    }
    File->Text = *Text;
    Cosmos::Parser P(File->Text, Path);
    File->Parsed = P.Parse();
    return File;
}

std::vector<Cosmos::Token> TokensOf(std::string_view Source, const std::string& Path) {
    Cosmos::Tokenizer          Lexer(Source, Path);
    std::vector<Cosmos::Token> Tokens;
    while (true) {
        Tokens.push_back(Lexer.NextToken());
        if (Tokens.back().Kind == Cosmos::TokenKind::EndOfFile) {
            break;
        }
    }
    return Tokens;
}

std::string_view LineOf(std::string_view Source, std::uint32_t Line) {
    std::size_t Start = 0;
    for (std::uint32_t Current = 1; Current < Line; ++Current) {
        const std::size_t Newline = Source.find('\n', Start);
        if (Newline == std::string_view::npos) {
            return {};
        }
        Start = Newline + 1;
    }
    const std::size_t End = Source.find('\n', Start);
    return Source.substr(Start, End == std::string_view::npos ? std::string_view::npos : End - Start);
}

// file:line:column: severity: message, then the line with the span underlined.
void RenderDiagnostic(std::ostream& Err, std::string_view Severity, const std::string& Message,
                      const Cosmos::SourceLocation& Start, const Cosmos::SourceLocation& End,
                      const ParsedFile& File) {
    Err << Start.FilePath << ":" << Start.Line << ":" << Start.Column << ": " << Severity << ": " << Message << "\n";
    const std::string_view Line = LineOf(File.Text, Start.Line);
    if (Start.Line == 0 || Start.Column == 0 || Start.Column > Line.size() + 1) {
        return;
    }
    std::size_t Width = 1;
    if (End.Line == Start.Line && End.Column > Start.Column) {
        Width = End.Column - Start.Column;
    } else if (End.Line > Start.Line) {
        Width = Line.size() + 1 - Start.Column;
    } else {
        for (const Cosmos::Token& T : TokensOf(File.Text, File.Path)) {
            if (T.Kind != Cosmos::TokenKind::EndOfFile && T.Location.Line == Start.Line &&
                T.Location.Column == Start.Column && T.End.Line == Start.Line && T.End.Column > Start.Column) {
                Width = T.End.Column - Start.Column;
                break;
            }
        }
    }
    const std::string Number = std::to_string(Start.Line);
    Err << " " << Number << " | " << Line << "\n";
    Err << " " << std::string(Number.size(), ' ') << " | " << std::string(Start.Column - 1, ' ')
        << std::string(std::max<std::size_t>(Width, 1), '^') << "\n";
}

std::size_t RenderParseErrors(const ParsedFile& File, std::ostream& Err) {
    for (const Cosmos::ParseError& Error : File.Parsed.Errors) {
        RenderDiagnostic(Err, "error", Error.Message, Error.Location, Error.Location, File);
    }
    return File.Parsed.Errors.size();
}

bool SkippedFolder(const std::filesystem::path& Folder) {
    const std::string Name = Folder.filename().string();
    return (Name.starts_with('.') && Name != "." && Name != "..") || Name == "build" || Name.starts_with("build-") ||
           Name.starts_with("build_") || Name == "node_modules" || Name == "_deps";
}


} // namespace

std::vector<std::string> CosmosFilesUnder(const std::filesystem::path& Folder) {
    std::vector<std::string> Files;
    std::error_code          Error;
    std::filesystem::recursive_directory_iterator It(Folder, std::filesystem::directory_options::skip_permission_denied,
                                                     Error);
    for (; !Error && It != std::filesystem::recursive_directory_iterator(); It.increment(Error)) {
        if (It->is_directory(Error) && SkippedFolder(It->path())) {
            It.disable_recursion_pending();
            continue;
        }
        if (It->is_regular_file(Error) && It->path().extension() == ".cosmos") {
            Files.push_back(It->path().string());
        }
    }
    std::ranges::sort(Files);
    return Files;
}

namespace {

bool IsGlobalPath(const std::string& Path) { return std::filesystem::path(Path).filename() == "global.cosmos"; }

std::string SiblingGlobalPath(const std::string& Path) {
    return (std::filesystem::path(Path).parent_path() / "global.cosmos").string();
}

int CheckCommand(const std::vector<std::string>& Arguments, std::ostream& Out, std::ostream& Err) {
    if (Arguments.empty()) {
        return UsageError("'cosmos check' takes at least one file or directory", Err);
    }
    std::vector<std::string> Files;
    for (const std::string& Argument : Arguments) {
        if (Argument.starts_with("--")) {
            return UsageError("'cosmos check' has no option '" + Argument + "'", Err);
        }
        if (std::filesystem::is_directory(Argument)) {
            std::ranges::move(CosmosFilesUnder(Argument), std::back_inserter(Files));
        } else if (std::filesystem::path(Argument).extension() != ".cosmos") {
            Err << "note: skipping '" << Argument << "', which isn't a .cosmos file\n";
        } else {
            Files.push_back(Argument);
        }
    }
    std::vector<std::string> Unique;
    for (const std::string& Path : Files) {
        const auto Same = [&](const std::string& Kept) {
            std::error_code Error;
            return Kept == Path || std::filesystem::equivalent(Kept, Path, Error);
        };
        if (std::ranges::none_of(Unique, Same)) {
            Unique.push_back(Path);
        }
    }
    Files = std::move(Unique);
    if (Files.empty()) {
        Err << "warning: no .cosmos files to check\n";
        return kExitSuccess;
    }

    std::map<std::string, std::unique_ptr<ParsedFile>> Globals;
    auto GlobalBeside = [&](const std::string& Path) -> const Cosmos::Stylesheet* {
        const std::string GlobalPath = SiblingGlobalPath(Path);
        auto              Found      = Globals.find(GlobalPath);
        if (Found == Globals.end()) {
            Found = Globals.emplace(GlobalPath, std::filesystem::is_regular_file(GlobalPath) ? ParseFile(GlobalPath)
                                                                                             : nullptr)
                        .first;
        }
        return Found->second && Found->second->Parsed.Sheet ? &*Found->second->Parsed.Sheet : nullptr;
    };

    std::size_t Errors   = 0;
    std::size_t Warnings = 0;
    for (const std::string& Path : Files) {
        const std::unique_ptr<ParsedFile> File = ParseFile(Path);
        if (!File) {
            Err << "error: can't read '" << Path << "'\n";
            ++Errors;
            continue;
        }
        Errors += RenderParseErrors(*File, Err);
        if (!File->Parsed.Sheet) {
            continue;
        }
        Cosmos::StylesheetSet Sheets;
        if (IsGlobalPath(Path)) {
            Sheets.Global = &*File->Parsed.Sheet;
        } else {
            Sheets.Global    = GlobalBeside(Path);
            Sheets.Component = &*File->Parsed.Sheet;
        }
        for (const Cosmos::CheckDiagnostic& Found : Cosmos::CheckStylesheet(Sheets)) {
            if (!Found.Location.FilePath.empty() && Found.Location.FilePath != Path) {
                continue;
            }
            const bool IsError = Found.Severity == Cosmos::CheckSeverity::Error;
            RenderDiagnostic(Err, IsError ? "error" : "warning", Found.Message, Found.Location, Found.End, *File);
            ++(IsError ? Errors : Warnings);
        }
    }

    if (Errors > 0 || Warnings > 0) {
        Err << "checked " << Files.size() << (Files.size() == 1 ? " file" : " files") << ": " << Errors
            << (Errors == 1 ? " error" : " errors") << ", " << Warnings << (Warnings == 1 ? " warning" : " warnings")
            << "\n";
    }
    (void)Out;
    return Errors > 0 ? kExitFailure : kExitSuccess;
}

std::string_view KindName(Cosmos::TokenKind Kind) {
    switch (Kind) {
    case Cosmos::TokenKind::ClassName: return "ClassName";
    case Cosmos::TokenKind::Identifier: return "Identifier";
    case Cosmos::TokenKind::PseudoClass: return "PseudoClass";
    case Cosmos::TokenKind::AtKeyword: return "AtKeyword";
    case Cosmos::TokenKind::VariableName: return "VariableName";
    case Cosmos::TokenKind::VarKeyword: return "VarKeyword";
    case Cosmos::TokenKind::RootKeyword: return "RootKeyword";
    case Cosmos::TokenKind::Number: return "Number";
    case Cosmos::TokenKind::StringLiteral: return "StringLiteral";
    case Cosmos::TokenKind::HexColor: return "HexColor";
    case Cosmos::TokenKind::OpenBrace: return "OpenBrace";
    case Cosmos::TokenKind::CloseBrace: return "CloseBrace";
    case Cosmos::TokenKind::OpenParen: return "OpenParen";
    case Cosmos::TokenKind::CloseParen: return "CloseParen";
    case Cosmos::TokenKind::Colon: return "Colon";
    case Cosmos::TokenKind::Semicolon: return "Semicolon";
    case Cosmos::TokenKind::Comma: return "Comma";
    case Cosmos::TokenKind::EndOfFile: return "EndOfFile";
    case Cosmos::TokenKind::Invalid: return "Invalid";
    case Cosmos::TokenKind::Comment: return "Comment";
    }
    return "Invalid";
}

int TokensCommand(const std::vector<std::string>& Arguments, std::ostream& Out, std::ostream& Err) {
    if (Arguments.size() != 1 || Arguments.front().starts_with("--")) {
        return UsageError("'cosmos tokens' takes one file", Err);
    }
    const std::optional<std::string> Text = ReadFile(Arguments.front());
    if (!Text) {
        Err << "error: can't read '" << Arguments.front() << "'\n";
        return kExitFailure;
    }
    bool AnyInvalid = false;
    for (const Cosmos::Token& T : TokensOf(*Text, Arguments.front())) {
        if (T.Kind == Cosmos::TokenKind::EndOfFile) {
            break;
        }
        AnyInvalid = AnyInvalid || T.Kind == Cosmos::TokenKind::Invalid;
        Out << T.Location.Line << ":" << T.Location.Column << "  " << KindName(T.Kind);
        if (!T.Text.empty()) {
            Out << "  '" << T.Text << "'";
        }
        Out << "\n";
    }
    return AnyInvalid ? kExitFailure : kExitSuccess;
}

int AstCommand(const std::vector<std::string>& Arguments, std::ostream& Out, std::ostream& Err) {
    bool                     Locations = false;
    std::vector<std::string> Files;
    for (const std::string& Argument : Arguments) {
        if (Argument == "--locations") {
            Locations = true;
        } else if (Argument.starts_with("--")) {
            return UsageError("'cosmos ast' has no option '" + Argument + "'", Err);
        } else {
            Files.push_back(Argument);
        }
    }
    if (Files.size() != 1) {
        return UsageError("'cosmos ast' takes one file", Err);
    }
    const std::unique_ptr<ParsedFile> File = ParseFile(Files.front());
    if (!File) {
        Err << "error: can't read '" << Files.front() << "'\n";
        return kExitFailure;
    }
    const std::size_t Errors = RenderParseErrors(*File, Err);
    if (File->Parsed.Sheet) {
        Out << Amanuensis::Writer::WriteToString(StylesheetToJson(*File->Parsed.Sheet, Locations),
                                                 Amanuensis::WriterOptions{});
    }
    return Errors > 0 ? kExitFailure : kExitSuccess;
}

std::string Where(const Cosmos::SourceLocation& Location) {
    return Location.FilePath + ":" + std::to_string(Location.Line) + ":" + std::to_string(Location.Column);
}

// Value with each attr(name) whose prop the path gives replaced by the prop's value, and a
// note naming where those values came from or which props are missing.
std::string WithProps(const std::string& Value, const std::map<std::string, std::string>& Props) {
    std::string              Text;
    std::vector<std::string> Used;
    std::vector<std::string> Missing;
    std::size_t              Pos = 0;
    while (true) {
        const std::size_t Start = Value.find("attr(", Pos);
        if (Start == std::string::npos) {
            Text += Value.substr(Pos);
            break;
        }
        const std::size_t Close = Value.find(')', Start);
        if (Close == std::string::npos) {
            Text += Value.substr(Pos);
            break;
        }
        const std::string Name = Value.substr(Start + 5, Close - Start - 5);
        Text += Value.substr(Pos, Start - Pos);
        if (const auto Prop = Props.find(Name); Prop != Props.end()) {
            Text += Prop->second;
            Used.push_back("attr(" + Name + ")");
        } else {
            Text += Value.substr(Start, Close + 1 - Start);
            Missing.push_back("`" + Name + "`");
        }
        Pos = Close + 1;
    }
    auto Join = [](const std::vector<std::string>& Parts) {
        std::string Joined;
        for (const std::string& Part : Parts) {
            Joined += (Joined.empty() ? "" : ", ") + Part;
        }
        return Joined;
    };
    if (!Used.empty()) {
        Text += "  (from " + Join(Used) + ")";
    }
    if (!Missing.empty()) {
        Text += "  (no " + Join(Missing) + (Missing.size() == 1 ? " prop" : " props") + " given)";
    }
    return Text;
}

void PrintStyle(const Cosmos::ResolvedStyle& Style, const std::vector<std::string>& Inherited,
                const std::map<std::string, std::string>& Props, std::ostream& Out) {
    const std::vector<Cosmos::StyleProperty> Properties = Cosmos::DescribeStyle(Style);
    if (Properties.empty()) {
        Out << "  (nothing set)\n";
    }
    for (const Cosmos::StyleProperty& Property : Properties) {
        Out << "  " << Property.Name << ": " << WithProps(Property.Value, Props);
        if (std::ranges::find(Inherited, Property.Name) != Inherited.end()) {
            Out << "  (inherited)";
        }
        Out << "\n";
    }
}

std::optional<Cosmos::PseudoKind> PseudoOf(Cosmos::StyleState State) {
    switch (State) {
    case Cosmos::StyleState::Hover: return Cosmos::PseudoKind::Hover;
    case Cosmos::StyleState::Active: return Cosmos::PseudoKind::Active;
    case Cosmos::StyleState::Disabled: return Cosmos::PseudoKind::Disabled;
    default: return std::nullopt;
    }
}

int ExplainCommand(const std::vector<std::string>& Arguments, std::ostream& Out, std::ostream& Err) {
    std::optional<std::size_t> RootNumber;
    std::optional<std::string> GlobalOverride;
    std::vector<std::string>   Positional;
    for (std::size_t I = 0; I < Arguments.size(); ++I) {
        const std::string& Argument = Arguments[I];
        if (Argument == "--root" || Argument == "--global") {
            if (I + 1 >= Arguments.size()) {
                return UsageError("'" + Argument + "' needs a value", Err);
            }
            const std::string& Value = Arguments[++I];
            if (Argument == "--global") {
                GlobalOverride = Value;
                continue;
            }
            std::size_t Number   = 0;
            const auto  Parsed   = std::from_chars(Value.data(), Value.data() + Value.size(), Number);
            if (Parsed.ec != std::errc{} || Parsed.ptr != Value.data() + Value.size() || Number == 0) {
                return UsageError("'--root' takes an element's position in the path, from 1", Err);
            }
            RootNumber = Number;
        } else if (Argument.starts_with("--")) {
            return UsageError("'cosmos explain' has no option '" + Argument + "'", Err);
        } else {
            Positional.push_back(Argument);
        }
    }
    if (Positional.size() != 2) {
        return UsageError("'cosmos explain' takes a file and an element path", Err);
    }
    const std::string& Path = Positional[0];

    const auto ParsedPath = ParseElementPath(Positional[1]);
    if (const auto* Error = std::get_if<ElementPathError>(&ParsedPath)) {
        return UsageError("element path, column " + std::to_string(Error->Column) + ": " + Error->Message, Err);
    }
    const auto& Elements = std::get<std::vector<ElementDescription>>(ParsedPath);
    const std::size_t RootIndex = RootNumber.value_or(1) - 1;
    if (RootIndex >= Elements.size()) {
        return UsageError("'--root " + std::to_string(*RootNumber) + "' is past the path's " +
                              std::to_string(Elements.size()) + (Elements.size() == 1 ? " element" : " elements"),
                          Err);
    }

    const std::unique_ptr<ParsedFile> File = ParseFile(Path);
    if (!File) {
        Err << "error: can't read '" << Path << "'\n";
        return kExitFailure;
    }
    std::size_t Errors = RenderParseErrors(*File, Err);

    std::unique_ptr<ParsedFile> Global;
    if (GlobalOverride) {
        Global = ParseFile(*GlobalOverride);
        if (!Global) {
            Err << "error: can't read '" << *GlobalOverride << "'\n";
            return kExitFailure;
        }
        Errors += RenderParseErrors(*Global, Err);
    } else if (!IsGlobalPath(Path) && std::filesystem::is_regular_file(SiblingGlobalPath(Path))) {
        Global = ParseFile(SiblingGlobalPath(Path));
    }

    Cosmos::StylesheetSet Sheets;
    if (IsGlobalPath(Path) && !GlobalOverride) {
        Sheets.Global = File->Parsed.Sheet ? &*File->Parsed.Sheet : nullptr;
    } else {
        Sheets.Global    = Global && Global->Parsed.Sheet ? &*Global->Parsed.Sheet : nullptr;
        Sheets.Component = File->Parsed.Sheet ? &*File->Parsed.Sheet : nullptr;
    }

    const ElementChain             Chain(Elements, RootIndex);
    const Cosmos::StyleExplanation Explanation = Cosmos::ExplainStyle(Chain.Target(), Sheets);
    const ElementDescription&      Element     = Elements.back();

    Out << DescribeElement(Element) << "\n";
    for (std::size_t I = Elements.size() - 1; I-- > 0;) {
        Out << "  inside " << DescribeElement(Elements[I]) << (I == RootIndex ? "  (component root)" : "") << "\n";
    }
    if (RootIndex + 1 == Elements.size()) {
        Out << "  (component root)\n";
    }

    std::vector<Cosmos::SourceLocation> Explained;
    Out << "\nRules, in cascade order:\n";
    bool AnyRule = false;
    for (const Cosmos::ExplainedRule& R : Explanation.Rules) {
        const std::optional<Cosmos::PseudoKind> RulePseudo = R.Lineage.back()->Pseudo;
        if (RulePseudo && Element.Pseudo && *RulePseudo != *Element.Pseudo) {
            continue;
        }
        AnyRule = true;
        Out << "  " << Where(R.Lineage.back()->Location) << "  " << Cosmos::SelectorChainText(R.Lineage) << "\n";
        bool InStarting = false;
        for (const Cosmos::ExplainedDeclaration& D : R.Declarations) {
            if (D.State == Cosmos::StyleState::Starting && !InStarting) {
                Out << "    @starting-style\n";
                InStarting = true;
            }
            Out << (InStarting ? "      " : "    ") << D.Written << ";";
            if (!D.Resolved.empty()) {
                Out << "  = " << D.Resolved;
            }
            if (!D.Problem.empty()) {
                Out << "  ignored: " << D.Problem;
                Explained.push_back(D.Decl->Location);
            } else if (D.OverriddenBy) {
                Out << "  overridden by " << Where(D.OverriddenBy->Location);
            }
            Out << "\n";
        }
    }
    if (!AnyRule) {
        Out << "  (none match)\n";
    }

    Out << "\nStyle:\n";
    PrintStyle(Explanation.Style, Explanation.InheritedProperties, Element.Props, Out);

    const std::vector<std::pair<std::shared_ptr<Cosmos::ResolvedStyle>, Cosmos::StyleState>> Overlays{
        {Explanation.Style.Hover, Cosmos::StyleState::Hover},
        {Explanation.Style.Active, Cosmos::StyleState::Active},
        {Explanation.Style.Disabled, Cosmos::StyleState::Disabled},
        {Explanation.Style.Starting, Cosmos::StyleState::Starting}};
    for (const auto& [Overlay, State] : Overlays) {
        const std::optional<Cosmos::PseudoKind> Pseudo = PseudoOf(State);
        if (!Overlay || (Pseudo && Element.Pseudo && *Pseudo != *Element.Pseudo)) {
            continue;
        }
        Out << "\n";
        switch (State) {
        case Cosmos::StyleState::Hover: Out << "While hovered, on top of the style above:\n"; break;
        case Cosmos::StyleState::Active: Out << "While pressed, on top of the style above:\n"; break;
        case Cosmos::StyleState::Disabled: Out << "While disabled, on top of the style above:\n"; break;
        default: Out << "When it appears (@starting-style), on top of the style above:\n"; break;
        }
        PrintStyle(*Overlay, {}, Element.Props, Out);
    }

    auto InExplainedRule = [&](const Cosmos::SourceLocation& At) {
        return std::ranges::any_of(Explanation.Rules, [&](const Cosmos::ExplainedRule& R) {
            const Cosmos::Rule& Matched = *R.Lineage.back();
            const bool After  = At.Line > Matched.Location.Line ||
                               (At.Line == Matched.Location.Line && At.Column >= Matched.Location.Column);
            const bool Before = At.Line < Matched.End.Line ||
                                (At.Line == Matched.End.Line && At.Column < Matched.End.Column);
            return At.FilePath == Matched.Location.FilePath && After && Before;
        });
    };
    for (const Cosmos::ResolveDiagnostic& Diagnostic : Explanation.Diagnostics) {
        if (!InExplainedRule(Diagnostic.Location)) {
            continue;
        }
        const bool Shown = std::ranges::any_of(Explained, [&](const Cosmos::SourceLocation& At) {
            return At.FilePath == Diagnostic.Location.FilePath && At.Line == Diagnostic.Location.Line &&
                   At.Column == Diagnostic.Location.Column;
        });
        if (Shown) {
            continue;
        }
        const ParsedFile* Owner = Global && Diagnostic.Location.FilePath == Global->Path ? Global.get() : File.get();
        RenderDiagnostic(Err, "warning", Diagnostic.Message, Diagnostic.Location, Diagnostic.End, *Owner);
    }
    return Errors > 0 ? kExitFailure : kExitSuccess;
}

using Command = std::function<int(const std::vector<std::string>&, std::ostream&, std::ostream&)>;

const std::map<std::string, Command>& Commands() {
    static const std::map<std::string, Command> Table{
        {"check", CheckCommand},
        {"tokens", TokensCommand},
        {"ast", AstCommand},
        {"explain", ExplainCommand},
        {"fmt", FormatCommand},
    };
    return Table;
}

} // namespace

int RunCosmosCli(const std::vector<std::string>& Arguments, std::ostream& Out, std::ostream& Err) {
    if (Arguments.empty()) {
        Err << kUsage;
        return kExitUsage;
    }
    const std::string& Name = Arguments.front();
    if (Name == "help" || Name == "--help" || Name == "-h") {
        Out << kUsage;
        return kExitSuccess;
    }
    const auto Command = Commands().find(Name);
    if (Command == Commands().end()) {
        return UsageError("unknown command '" + Name + "'", Err);
    }
    return Command->second(std::vector<std::string>(Arguments.begin() + 1, Arguments.end()), Out, Err);
}

} // namespace CosmosCli
