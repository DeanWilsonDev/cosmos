# lustre

Lustre is the actively developed prototype on the `proto` branch of
[`cosmos`](https://github.com/DeanWilsonDev/cosmos), renamed from `lustre`.
Its original Git history is preserved. `main` is a README-only starting point
for the future canonical Cosmos implementation. Existing Lustre names, file
extensions and build targets are retained on this branch.

> **Status:** Core parser/resolver and an editor LSP exist; runtime hot-reload wiring
> into Iris is still design-phase (see `docs/lustre_handoff.md` §7's open questions).

Lustre is the styling and animation layer for [Iris](https://github.com/DeanWilsonDev/chaos/tree/proto).
Where `.iris`/`.irisx` files define structure (component composition, the `render { }` element
tree), `.lustre` files are the sole authoring surface for appearance — colors, layout-adjacent
visual properties, transitions — resolved down to concrete values per rendering backend.

## Why this exists

Iris Core deliberately does not define Lustre's syntax, cascade rules, or how its properties
map onto backend-specific style structs (Penumbra's `BoxStyle`/`ButtonStyle`/etc., or a future
Umbra Engine backend's own equivalents) — see `iris`'s `docs/iris_core_spec.md` §4/§8 and
`docs/iris_handoff.md` §4. That is Lustre's own design problem, scoped here instead.

What Iris Core already fixes, and Lustre must live within:

- Style is never inline in `.iris` files — no `style="..."` prop, ever.
- The `class` prop is valid on any element and accepts a string class name; it is the sole
  join between an Iris element and its Lustre declarations.
- Iris ships no default styling opinions — Lustre resolution is what actually reads a
  `class` value and produces concrete style values.

## Relationship to Iris's roadmap

This is Iris's Stage 4 (`iris`'s `docs/iris_handoff.md` §6 / `docs/iris_next_steps.md`):
Stages 0–3 (language spec, preprocessor front end, Penumbra backend, reactive runtime) are
done. Stage 4 — Lustre-lite style resolution — has not been designed yet; this repo is where
that design happens, and where the resulting compiler/resolver will eventually live.

## What's here

- `docs/` — design docs, decision records, and open questions, following the same pattern
  `iris`'s own `docs/` directory used during its Stage 0 scoping (a handoff doc first, decision
  docs as design questions get resolved).
- `include/Lustre/`, `src/Lustre/` — the core library: `Tokenizer`/`Parser` (parses one
  `.lustre` file into the `Ast.h` tree) and `Resolver` (cascade + variable resolution down to
  `ResolvedStyle`, backend-agnostic).
- `tools/lustre-lsp/` — a stdio LSP server for `.lustre` files (completion, goto-definition,
  diagnostics — see `docs/lustre_lsp_decision.md`).
- `editors/nvim/` — Neovim wiring for `lustre-lsp` and syntax highlighting.

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

This builds the `lustre` static library, `lustre_lsp` (the LSP server), and both test
executables. Run the test suites with:

```sh
./build/tests/test_lustre
./build/tools/lustre-lsp/tests/test_lustre_lsp
```

### Editor setup (Neovim)

1. Build `lustre_lsp` (above) and make sure it's on your `PATH` (e.g. symlink
   `build/lustre_lsp` into somewhere on `PATH`, or add `build/` to it).
2. Source both files in `editors/nvim/` from your Neovim config:

   ```lua
   dofile("/path/to/lustre/editors/nvim/lustre-lsp.lua")
   dofile("/path/to/lustre/editors/nvim/lustre-treesitter.lua")
   ```
3. Install the `css` tree-sitter parser if you don't already have it (`:TSInstall css`) —
   syntax highlighting for `.lustre` deliberately reuses it rather than a bespoke grammar
   (`docs/lustre_lsp_decision.md` §2).

Opening a `.lustre` file should then get you diagnostics, completion (property names,
per-property keyword values, `var(--name)` names in scope), goto-definition on
`var(--name)` references, and CSS-based syntax highlighting.
