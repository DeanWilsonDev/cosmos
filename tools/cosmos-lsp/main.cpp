// cosmos-lsp: an LSP server for `.cosmos` files, stdio transport only (the one
// transport every LSP client, including Neovim's built-in client, supports without
// extra configuration). See docs/cosmos_lsp_decision.md for the architecture this
// implements — unlike iris-lsp, there is no host-language proxy: `.cosmos` is a
// self-contained grammar with its own real Tokenizer/Parser, so every feature here is
// served directly from `libCosmos`.
//
// Usage: cosmos_lsp   (no arguments — LSP servers are always driven entirely by the
// initialize request's own params, never argv)

#include "Server.h"

int main() {
    CosmosLsp::Server Server;
    Server.Run(stdin, stdout);
    return 0;
}
