#pragma once

#include "cosmos/Ast.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CosmosLsp {

// A 1-based source span, End just past the last character.
struct TextSpan {
    std::uint32_t Line{1};
    std::uint32_t Column{1};
    std::uint32_t EndLine{1};
    std::uint32_t EndColumn{1};

    friend bool operator==(const TextSpan&, const TextSpan&) = default;
};

enum class NamedSymbolKind { Variable, Class, Property };

// What the cursor is on in a .cosmos file: a `--variable` (declared or in `var()`), a
// `.class` selector, or a declaration's property name.
struct NamedSymbol {
    NamedSymbolKind Kind{NamedSymbolKind::Variable};
    std::string     Name;
    TextSpan        Span;
    bool            IsDeclaration{false};
};

std::optional<NamedSymbol> SymbolAt(std::string_view Source, std::uint32_t Line, std::uint32_t Column);

struct Occurrence {
    TextSpan Span;
    bool     IsDeclaration{false};
};

// Every `--Name` in a .cosmos source: declarations and `var(--Name)` references. The span
// covers `--Name`.
std::vector<Occurrence> VariableOccurrences(std::string_view Source, std::string_view Name);

// Every `.Name` selector in a .cosmos source, each a declaration. The span covers `.Name`.
std::vector<Occurrence> ClassSelectorOccurrences(std::string_view Source, std::string_view Name);

// Every use of class Name in a .chaos source's `class="a b"` (or single-quoted) props. The
// span covers the name alone.
std::vector<Occurrence> ClassUsesInChaos(std::string_view Source, std::string_view Name);

// The class name under the cursor in a .chaos `class="..."` string, if any.
std::optional<NamedSymbol> ChaosClassAt(std::string_view Source, std::uint32_t Line, std::uint32_t Column);

// `.card:hover`, `text`, as written.
std::string SelectorText(const Cosmos::Rule& R);

} // namespace CosmosLsp
