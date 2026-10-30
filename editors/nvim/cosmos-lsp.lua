-- Minimal Neovim wiring for cosmos-lsp (docs/cosmos_lsp_decision.md). Source this from
-- init.lua, or copy it into an existing config -- there's no plugin to install, just
-- Neovim's own built-in LSP client (vim.lsp.start), same as any stdio LSP server.
--
-- Requires `cosmos_lsp` on PATH (built via `cmake --build build --target cosmos_lsp` in
-- this repo). Unlike iris-lsp, there's no host-language server dependency to degrade
-- around -- `.cosmos` is a self-contained grammar, so completion/goto-definition/
-- diagnostics all come straight from this one process.
--
-- Syntax highlighting is separate -- see cosmos-treesitter.lua alongside this file.
--
-- It also attaches to .chaos buffers (beside chaos-lsp, which owns them), so references
-- from a `class="card"` string find `.card`'s selectors and every other use. For a .chaos
-- buffer cosmos-lsp answers references and rename only, and publishes no diagnostics.

vim.filetype.add({ extension = { cosmos = "cosmos", chaos = "chaos" } })

vim.api.nvim_create_autocmd("FileType", {
  pattern = { "cosmos", "chaos" },
  callback = function(args)
    vim.lsp.start({
      name = "cosmos-lsp",
      cmd = { "cosmos_lsp" },
      root_dir = vim.fs.root(args.buf, { "global.cosmos", ".git" }) or vim.fs.dirname(args.buf),
    })
  end,
})
