---
description: Read and write Cosmos (.cosmos) stylesheets — a declarative, CSS-like styling language for Iris UI components. Use whenever a project has .cosmos files, a global.cosmos, or styles Iris components via a `class` prop.
---

Cosmos is a declarative, CSS-like DSL — deliberately **not**
Turing-complete (no logic, conditionals, or data binding) — that is the
sole styling and animation layer for Iris UI components. `.iris` files
define structure/behavior; `.cosmos` files define appearance, joined to
Iris elements purely through a `class` string prop. It's syntactically a
strict CSS subset, **runtime-loaded/interpreted** (not compiled ahead of
time, unlike Iris's own `iris_cc`), specifically to support hot-reloading
styles without a rebuild. The authoritative reference is
`docs/cosmos_core_spec.md` in this repo; this skill is a summary of it.

## Build & consume

```sh
git submodule update --init --recursive
cmake -S . -B build
cmake --build build -j"$(nproc)"

./build/tests/test_cosmos
./build/tools/cosmos-lsp/tests/test_cosmos_lsp
```

There is **no standalone `cosmos` CLI compiler** — Cosmos is consumed by
linking `libcosmos.a` and driving `Cosmos::Parser`/`Cosmos::Resolver`
programmatically (a host implements `Cosmos::IStyleTarget` over its own
widget tree and calls `Resolver::Resolve(...)`). Only the library and an
LSP server (`cosmos_lsp`) exist as artifacts.

## File model

- `Name.cosmos` — component-scoped stylesheet, paired 1:1 with
  `Name.iris`/`Name.irisx`.
- `global.cosmos` — project-wide baseline styles/variables/cross-component
  rules; the only file that can select across component boundaries.

## Selectors — three kinds, never combined

```css
.card { }              /* class selector */
frame { }               /* primitive-element selector, lowercase */
.card:hover { }          /* pseudo-class, suffixed onto a class or primitive */
```

`Frame.card { }` (compound tag+class) is **invalid syntax** — a parse
error. An element's `class` can list several names, `class="card selected"`;
`.card` and `.selected` both match it, and where they set the same property
the later rule in the sheet wins. Primitive selectors map to Iris's PascalCase tags via a closed
table: `frame→Frame`, `inline→Inline`, `grid→Grid`, `image→Image`,
`text→Text`, `scroll→Scroll`, `input→Input`, `textarea→TextArea`. **`icon`/`Icon` is not in this
table yet** — a known, documented gap; you cannot select `<Icon>` by
primitive selector.

## Nesting = real descendant selectors

```css
.card {
    padding: 16px;

    .card-title {
        font-size: 20px;
    }
}

.card:hover {
    .card-title {
        color: var(--color-text-emphasis);
    }
}
```

`.card-title` here only matches an element with `class="card-title"` that
is an actual descendant (any depth) of a `.card`-classed element — this is
real specificity, not file-organization sugar.

**Component-boundary rule (resolve-time, not a compile error):** a
component's own `.cosmos` file cannot select into a nested child
component's internals. `Card.cosmos`'s `.card .health-bar-fill` never
matches inside a mounted `<HealthBar/>`, even if `HealthBar.cosmos` itself
defines `.health-bar-fill`. This is **silent** — a selector that crosses a
component boundary simply matches nothing, no diagnostic. Only
`global.cosmos` is unbounded. This is the first thing to check when a style
"isn't applying."

## Cascade

Exactly two layers, no specificity system beyond nesting depth:

1. `global.cosmos` — baseline
2. Component's own `Name.cosmos` — overrides global for anything both define

No `!important`, no third tier.

## Variables

```css
:root {
    --color-primary: #E8593C;
    --spacing-md: 16px;
}
```

Declared with a `--` prefix, either inside `:root { }` (global.cosmos) or
directly inside a component's top-level class block (component-scoped —
shadows a same-named global variable within that file only). Referenced via
`var(--name)`. **No fallback argument** — `var(--x, fallback)` is invalid
syntax. An unresolved reference is a **compile-time error**, never a silent
fallback.

## Units

| Unit | Resolves against | Status |
|---|---|---|
| `px` | Direct logical-pixel value | real |
| `%` | Parent's computed size | real |
| `vw`/`vh` | Application window's current logical size | real |
| `rem` / `em` | — | **stubbed**, no root-font-size convention exists yet |

`%`, `vw` and `vh` only work on `width`, `height`, `min-width` and `max-width`,
and are resolved at layout time, so they follow resizes. Anywhere else they're
a resolve diagnostic and the declaration is dropped. `width: 600px; max-width:
100%;` is the way to write a dialog panel that shrinks in a narrow window.

Only `margin` takes a negative length (`margin: 0px 0px 0px -6px;`), which pulls
an element past its parent's content edge, for example so a button's hover
padding bleeds out while its text lines up with the content above it. A negative
length on any other property is a resolve diagnostic, and the declaration is
dropped.

## Property reference (selected — full real-vs-stubbed table in `docs/cosmos_core_spec.md` §2)

**Real (applied by the Penumbra backend today):** `background-color`,
`background-gradient-start`/`-end`, `border-color`, `border-width`,
`border-radius` (single uniform value, no per-corner shorthand),
`padding`/`margin` (1–4 value CSS shorthand), `color` (text only),
`font-family`/`font-size` (the family is a font file path, which penumbra-ui-backend resolves
relative to the stylesheet; each inherits on its own, and a size with no family anywhere uses
the app's default font), `display: stack|inline`, `flex-direction:
row|column`, `gap`, `align-items: start|center|end|stretch`, `width`,
`height`, `min-width`, `max-width` (on `text`, a px `max-width` is the
truncation width),
`text-overflow: clip|ellipsis` (needs `max-width` set to take effect),
`white-space: nowrap|normal` (`normal` word-wraps text across lines to fit
the box's own width; mutually exclusive with `max-width`/`text-overflow`
in practice), `font-style: normal|italic` (inherits; synthetic italic, so it
works on a font with no italic face), `text-decoration: none|underline|line-through`
(`underline line-through` sets both; does not inherit),
`transition` (color-only, e.g. `background-color 0.2s` — no easing
keyword), `box-shadow` (color + blur radius only, e.g. `box-shadow:
#000000AA 12px;` — no multi-layer list), `flex-grow: <number>` (a stack
child shares the main-axis space its siblings and gaps leave, in proportion
to its number — `flex: 1`-style, the child's own content size is ignored;
the way to make a `<Scroll>` below a header end at its parent's bottom
edge), `scrollbar-color: <thumb> [<track>]` and `scrollbar-width:
<length>` (`<Scroll>`/`<TextArea>`; no thumb is drawn until both a thumb
colour and a width are set; a `:hover` `scrollbar-color` applies while the
pointer is over the panel).

**Stubbed (parsed into the IR, not applied by any backend yet):** `width`,
`height`, `transform: scale(<number>)`, most non-`background-color`
pseudo-class-scoped properties on non-Button widgets.

**Container-only property guard:** `display`, `flex-direction`, `gap`,
`align-items` are **rejected with a diagnostic** (not silently resolved)
when applied to a leaf primitive (`Image`/`Text`/`Input`) rather than a
container (`Frame`/`Grid`/`Scroll`/`Inline`). This was a real bug hit in a
consumer project — check container vs. leaf before reaching for a layout
property.

## Worked example

```cpp
// HealthBar.iris
struct HealthBarProps { float current; };

component HealthBar(props: HealthBarProps) {
    std::string barClass = props.current < 25.0f ? "bar-critical" : "bar-normal";
    render {
        <Frame class="health-bar">
            <Frame class={barClass} />
        </Frame>
    }
}
```

```css
/* HealthBar.cosmos */
.health-bar {
    --bar-background: #333333;
    background-color: var(--bar-background);
    border-radius: 8px;
}

.bar-normal    { background-color: #4CAF50; }
.bar-critical  { background-color: #E8593C; }
```

## Errors to expect

| Error | Trigger |
|---|---|
| Undefined variable | `var(--x)` not declared globally or in current scope |
| Duplicate selector | Two rule blocks with an identical selector in one file |
| Compound tag+class selector | `Frame.card { }` |
| Container-only property on a leaf | `display`/`flex-direction`/`gap`/`align-items` on `Image`/`Text`/`Input` |

**Deliberately not diagnosed:** component-boundary crossing (matches
nothing, silently), general property-applicability (e.g. `frame {
font-family: ...; }` is accepted without complaint in v1).

## Tooling

- **`cosmos_lsp`** — stdio LSP server. Diagnostics are parse-only (compound
  selector, duplicate selector) — it does **not** do resolver-backed
  diagnostics (undefined variable, component-boundary), since those need
  whole-project context a single buffer doesn't have. Completion covers
  property names/pseudo-classes, keyword values for closed-enum properties,
  and `var(--name)` names in scope (this file + sibling `global.cosmos`).
  Goto-declaration works for `var(--name)`. Reparses the whole buffer on
  every change — no debouncing.
- **No formatter, no linter, no CLI compiler.**
- Syntax highlighting reuses tree-sitter's `css` grammar directly (no
  bespoke Cosmos grammar) — `:TSInstall css`.
- Neovim setup: `editors/nvim/cosmos-lsp.lua` +
  `editors/nvim/cosmos-treesitter.lua`.

## Things that will bite an agent writing Cosmos

- **Corrected 2026-09-14** — this used to say "runtime wiring into Iris is still
  design-phase"; that was stale/wrong. Static resolution *is* wired in and in
  production use: `penumbra-ui-backend`'s `Cosmos/StyleApplier.cpp`/
  `StyleResolution.cpp`/`StylesheetLoader.cpp` resolve a mounted Iris component's
  `class` against a real `StylesheetSet` at build/mount time (`pharos-proto`'s
  `App.irisx`/`InspectorPanel.irisx` etc. do this today, real `.cosmos` files,
  real resolved styles). What's still genuinely open is narrower than the old
  claim: *hot-reload specifically* — re-resolving styles when a `.cosmos` file
  changes on disk without a full remount/rebuild — doesn't exist yet. Don't
  conflate "hot-reload not built" with "styling doesn't work."
- `rem`/`em`, `width`/`height`, and `transform: scale(...)` are parsed but
  **do nothing** in the current backend — don't rely on them for layout.
- `transition` only animates color, and only with a fixed curve — no easing
  keyword, no `@keyframes`.
- Pseudo-class styles (`:hover`/`:active`/`:disabled`) only have a real
  backend field for `background-color` on Button-mapped widgets today;
  targeting other widgets resolves into the IR but has nowhere to land.
- No universal (`*`), attribute, or sibling (`+`/`~`) selectors exist.
- Only single-layer `box-shadow` is supported — a multi-layer list was
  explicitly declined, not just unimplemented.
- If a style silently isn't applying, check the component-boundary rule
  first — it's the most common "why isn't this working" cause and produces
  no diagnostic.

## Project structure reference

```
cosmos/
├── README.md
├── include/Cosmos/         — Ast.h, Parser.h, Resolver.h (IStyleTarget), ResolvedStyle.h, Token.h
├── src/Cosmos/              — Tokenizer.cpp, Parser.cpp, Resolver.cpp
├── tools/cosmos-lsp/         — stdio LSP server (Server, CompletionContext, JsonRpc)
├── tests/                     — test_cosmos (TokenizerTests, ParserTests, ResolverTests)
├── docs/cosmos_core_spec.md    — authoritative v1 spec; read this for anything not covered here
└── editors/nvim/                 — cosmos-lsp.lua, cosmos-treesitter.lua
```
