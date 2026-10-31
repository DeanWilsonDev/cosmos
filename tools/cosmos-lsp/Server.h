#pragma once

#include "cosmos/Ast.h"
#include "cosmos/Checker.h"
#include "cosmos/Parser.h"
#include "Navigation.h"

#include <amanuensis.hpp>

#include <cstdio>
#include <filesystem>
#include <optional>
#include <vector>
#include <string>
#include <unordered_map>

namespace CosmosLsp {

// Replaces Original's text from StartOffset to EndOffset with Text.
struct LineReplacement {
    std::size_t StartOffset;
    std::size_t EndOffset;
    std::string Text;
};

// The whole-line replacements that turn Original into Formatted, fewest lines first.
std::vector<LineReplacement> LineEditsBetween(std::string_view Original, std::string_view Formatted);

// The whole server: one process, one client connection over stdio, request dispatch for
// the handful of LSP methods this tool implements (docs/cosmos_lsp_decision.md). Unlike
// iris-lsp, there is no host-language proxy and no VirtualDocument position-translation
// step — `.cosmos` is a self-contained grammar with its own real Tokenizer/Parser
// (Cosmos::Parser), so every open document is just reparsed in full on each change.
// Cosmos files are small stylesheets, so re-parsing on every keystroke is not worth
// debouncing for v1 (same call iris-lsp made for its own Iris::CompileFile step).
//
// Single-threaded: there is no background proxy thread here, so — unlike iris-lsp —
// no mutex is needed around Documents_; Run()'s read loop is the only thing that ever
// touches it.
class Server {
public:
    // Blocks reading JsonRpc messages from In until "exit" or EOF.
    void Run(std::FILE* In, std::FILE* Out);

private:
    struct OpenDocument {
        std::string        Text;
        Cosmos::ParseResult Parsed;
    };

    void HandleMessage(const Amanuensis::JsonValue& Message);
    void Reply(const Amanuensis::JsonValue& Id, Amanuensis::JsonValue Result);
    void ReplyError(const Amanuensis::JsonValue& Id, long long Code, const std::string& Message);
    void Notify(const std::string& Method, Amanuensis::JsonValue Params);

    void HandleInitialize(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params);
    void HandleInitialized();
    void HandleDidSave(const Amanuensis::JsonValue& Params);
    void HandleDidChangeWatchedFiles(const Amanuensis::JsonValue& Params);
    void HandleDidOpen(const Amanuensis::JsonValue& Params);
    void HandleDidChange(const Amanuensis::JsonValue& Params);
    void HandleDidClose(const Amanuensis::JsonValue& Params);
    void HandleCompletion(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params);
    void HandleDefinition(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params);
    void HandleHover(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params);
    void HandleReferences(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params);
    void HandleDocumentHighlight(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params);
    void HandleDocumentSymbol(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params);
    void HandleWorkspaceSymbol(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params);

    // A file's text: its open buffer when it's open, else the copy on disk.
    std::optional<std::string> TextOf(const std::string& Path) const;
    struct FileOccurrence {
        std::string Path;
        Occurrence  Found;
        bool        InChaos{false};
    };
    // Whether Path's own scope (its :root and every rule) declares the variable Name, so a
    // var() of it there means its own and not global.cosmos's.
    bool DeclaresVariable(const std::string& Path, std::string_view Name) const;
    // The symbol under the cursor in a .cosmos file, or the class under it in a .chaos one.
    std::optional<NamedSymbol> SymbolAtCursor(const std::string& Path, std::uint32_t Line, std::uint32_t Column) const;
    // Everywhere Symbol, found in Path, is declared or used: a variable declared in Path
    // only there, one from global.cosmos in every .cosmos beside it that doesn't declare its
    // own; a class in every
    // .cosmos beside Path and in the class strings of every .chaos below its folder.
    std::vector<FileOccurrence> OccurrencesOf(const std::string& Path, const NamedSymbol& Symbol) const;

    // The first place a rename of Symbol to NewName would collide with: a class already
    // named NewName anywhere OccurrencesOf looks, or a variable named NewName in any file the
    // old one's uses could be in, which a rename would capture or shadow.
    std::optional<FileOccurrence> FirstUseOf(const std::string& Path, const NamedSymbol& Symbol,
                                             const std::string& NewName) const;

    void HandlePrepareRename(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params);
    void HandleRename(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params);
    void HandleCodeAction(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params);
    void HandleDocumentColor(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params);
    void HandleColorPresentation(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params);
    void HandleFormatting(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params, bool OnlyRange);
    void HandleFoldingRange(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params);

    // The checker's findings for Doc that belong to it, with the sibling global.cosmos.
    std::vector<Cosmos::CheckDiagnostic> CheckDocument(const std::string& Path, const OpenDocument& Doc);

    std::vector<std::string> CosmosFilesBeside(const std::string& Path) const;
    std::vector<std::string> ChaosFilesUnder(const std::filesystem::path& Folder) const;

    // Reparses Documents_[Uri] from Text and publishes fresh diagnostics — the one path
    // both didOpen and didChange funnel through.
    void RebuildDocument(const std::string& Uri, std::string Text);
    void PublishDiagnostics(const std::string& Uri, const OpenDocument& Doc);

    // The global.cosmos beside Path: its open buffer when it's open, else the copy on disk.
    // Null for global.cosmos itself, or when there's none.
    const Cosmos::Stylesheet* GlobalSheetFor(const std::string& Path);
    std::string               GlobalPathFor(const std::string& Path) const;
    void                      GlobalChanged(const std::string& GlobalPath);

    std::FILE*  Out_{nullptr};
    bool        WatchGlobalFiles_{false};
    std::string RootPath_;

    std::unordered_map<std::string, OpenDocument>        Documents_;
    struct DiskSheet {
        Cosmos::ParseResult                            Parsed;
        std::optional<std::filesystem::file_time_type> Stamp;
    };
    std::unordered_map<std::string, DiskSheet> DiskGlobals_;
};

} // namespace CosmosLsp
