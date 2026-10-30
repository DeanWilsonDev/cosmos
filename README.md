# cosmos

Cosmos is the actively developed prototype on the `proto` branch of
[`cosmos`](https://github.com/DeanWilsonDev/cosmos). `main` is a README-only
starting point for the future canonical Cosmos implementation.

> **Status:** Core parser/resolver and an editor LSP exist; runtime hot-reload wiring
> into Iris is still design-phase (see `docs/cosmos_handoff.md` §7's open questions).

Cosmos is the styling and animation layer for [Iris](https://github.com/DeanWilsonDev/chaos/tree/proto).
Where `.iris`/`.irisx` files define structure (component composition, the `render { }` element
tree), `.cosmos` files are the sole authoring surface for appearance — colors, layout-adjacent
visual properties, transitions — resolved down to concrete values per rendering backend.

## Why this exists

Iris Core deliberately does not define Cosmos's syntax, cascade rules, or how its properties
map onto backend-specific style structs (Penumbra's `BoxStyle`/`ButtonStyle`/etc., or a future
Umbra Engine backend's own equivalents) — see `chaos`'s `docs/chaos_core_spec.md` §4/§8 and
`docs/iris_handoff.md` §4. That is Cosmos's own design problem, scoped here instead.

What Iris Core already fixes, and Cosmos must live within:

- Style is never inline in `.iris` files — no `style="..."` prop, ever.
- The `class` prop is valid on any element and accepts a string class name; it is the sole
  join between an Iris element and its Cosmos declarations.
- Iris ships no default styling opinions — Cosmos resolution is what actually reads a
  `class` value and produces concrete style values.

## Relationship to Iris's roadmap

This is Iris's Stage 4 (`iris`'s `docs/iris_handoff.md` §6 / `docs/iris_next_steps.md`):
Stages 0–3 (language spec, preprocessor front end, Penumbra backend, reactive runtime) are
done. Stage 4 — Cosmos-lite style resolution — has not been designed yet; this repo is where
that design happens, and where the resulting compiler/resolver will eventually live.

## What's here

- `docs/` — design docs, decision records, and open questions, following the same pattern
  `iris`'s own `docs/` directory used during its Stage 0 scoping (a handoff doc first, decision
  docs as design questions get resolved).
- `include/Cosmos/`, `src/Cosmos/` — the core library: `Tokenizer`/`Parser` (parses one
  `.cosmos` file into the `Ast.h` tree) and `Resolver` (cascade + variable resolution down to
  `ResolvedStyle`, backend-agnostic).
- `tools/cosmos-lsp/` — a stdio LSP server for `.cosmos` files: diagnostics (parse errors and
  `Cosmos::CheckStylesheet`'s checks), completion, go to definition, hover, references (classes
  in `.chaos` `class` strings too), highlights, an outline, workspace symbols, rename for
  variables and classes, quick fixes from the checker's suggestions, colour swatches with a
  picker, folding, and formatting — see
  `docs/cosmos_lsp_decision.md`.
- `tools/cosmos/` — the `cosmos` command-line tool (below).
- `editors/nvim/` — Neovim wiring for `cosmos-lsp` and syntax highlighting.

## Build

Requires CMake 3.30+ and a C++26 compiler: GCC 14+, Clang 19+, or a recent AppleClang.
On Windows, use clang-cl from Clang 19+; MSVC's cl.exe isn't supported.
Submodules (`libs/cimmerian`, `libs/amanuensis`) must be checked out first if you haven't
already:

```sh
git submodule update --init --recursive

cmake -S . -B build
cmake --build build -j"$(nproc)"
```

This builds the `cosmos` static library, `cosmos_lsp` (the LSP server), the `cosmos`
command-line tool (`build/tools/cosmos/cosmos`) and the test executables. A project that
fetches Cosmos builds only the library unless it sets `COSMOS_BUILD_LSP`,
`COSMOS_BUILD_CLI` or `COSMOS_BUILD_TESTS` on. Run the test suites with:

```sh
./build/tests/test_cosmos
./build/tools/cosmos-lsp/tests/test_cosmos_lsp
./build/tools/cosmos/tests/test_cosmos_cli
```

### The `cosmos` command

```sh
cosmos check src/ui                      # every .cosmos below, each with the global.cosmos beside it
cosmos fmt src/ui                        # format in place; --check lists what would change
cosmos tokens src/ui/Card.cosmos         # the tokenizer's output
cosmos ast --locations src/ui/Card.cosmos
cosmos explain src/ui/Card.cosmos 'Frame[class="card"] > Text[class="title" size=12]:hover'
```

- `check` prints each problem as `file:line:column: error|warning: message` with the line
  underlined, then a count, and exits 1 when there's an error (warnings alone exit 0), so
  it can run in CI. An unknown property is a warning: a host ignores it, as CSS does. It reports what cosmos-lsp reports. A file named on the command line
  that isn't `.cosmos` is skipped with a note, so `cosmos check src/ui/*` checks the same
  files as `cosmos check src/ui`.
- `explain` describes an element, not a selector: tags as Chaos writes them, each with
  its `class` and other props in `[...]`, outermost first, each the previous one's child,
  and an optional `:hover`, `:active` or `:disabled` on the last. It prints every rule that
  matches the last element in the order the resolver applies them (global.cosmos's, then
  the file's, by nesting depth and source order), each declaration's value after `var()`,
  which declarations are overridden or ignored and why, then the resolved style, with
  inherited properties marked and `attr()` values taken from the props. The first element
  is the component root unless `--root N` names another (counting from 1), and the global
  sheet is the `global.cosmos` beside the file unless `--global PATH` names another.
- `fmt` lays a stylesheet out with one declaration per line, blocks indented, a blank line
  between top-level blocks, at most one of the author's blank lines kept, and every comment
  kept on its line. A comma-separated value longer than the line width puts each group on
  its own line. It reads `lineWidth` (default 100) and `indentWidth` (default 4) from the
  `format` object of the nearest `.nyx.json` above the file, the same settings `nyx fmt` and
  `chaos fmt` use. A file with syntax errors is left alone, and output that wouldn't parse
  back to the same tree and comments is never written. `--check` writes nothing, lists the
  files that would change and exits 1 if there are any. cosmos-lsp formats the same way
  (`textDocument/formatting`, and `rangeFormatting`, which returns the changed blocks of
  lines that touch the range).
- Exit codes: 0 success, 1 problems found, 2 a usage mistake.

### Editor setup (Neovim)

1. Build `cosmos_lsp` (above) and make sure it's on your `PATH` (e.g. symlink
   `build/cosmos_lsp` into somewhere on `PATH`, or add `build/` to it).
2. Source both files in `editors/nvim/` from your Neovim config:

   ```lua
   dofile("/path/to/cosmos/editors/nvim/cosmos-lsp.lua")
   dofile("/path/to/cosmos/editors/nvim/cosmos-treesitter.lua")
   ```
3. Install the `css` tree-sitter parser if you don't already have it (`:TSInstall css`) —
   syntax highlighting for `.cosmos` deliberately reuses it rather than a bespoke grammar
   (`docs/cosmos_lsp_decision.md` §2).

Opening a `.cosmos` file should then get you everything listed under What's here, with
CSS-based syntax highlighting. `cosmos-lsp.lua` also attaches the server to `.chaos`
buffers, so references and rename on a class find its uses in `class="..."` strings; there
it answers only those requests and never publishes diagnostics.
