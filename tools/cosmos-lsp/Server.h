#pragma once

#include "Cosmos/Ast.h"
#include "Cosmos/Parser.h"

#include <amanuensis.hpp>

#include <cstdio>
#include <string>
#include <unordered_map>

namespace CosmosLsp {

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
    void Notify(const std::string& Method, Amanuensis::JsonValue Params);

    void HandleInitialize(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params);
    void HandleDidOpen(const Amanuensis::JsonValue& Params);
    void HandleDidChange(const Amanuensis::JsonValue& Params);
    void HandleDidClose(const Amanuensis::JsonValue& Params);
    void HandleCompletion(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params);
    void HandleDefinition(const Amanuensis::JsonValue& Id, const Amanuensis::JsonValue& Params);

    // Reparses Documents_[Uri] from Text and publishes fresh diagnostics — the one path
    // both didOpen and didChange funnel through.
    void RebuildDocument(const std::string& Uri, std::string Text);
    void PublishDiagnostics(const std::string& Uri, const OpenDocument& Doc);

    std::FILE* Out_{nullptr};

    std::unordered_map<std::string, OpenDocument> Documents_;
};

} // namespace CosmosLsp
