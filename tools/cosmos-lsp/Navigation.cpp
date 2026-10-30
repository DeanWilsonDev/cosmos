#include "Navigation.h"

#include "CompletionContext.h"

#include <cctype>

namespace CosmosLsp {

namespace {

TextSpan SpanOf(const Cosmos::Token& T) { return TextSpan{T.Location.Line, T.Location.Column, T.End.Line, T.End.Column}; }

bool Contains(const TextSpan& Span, std::uint32_t Line, std::uint32_t Column) {
    const bool AfterStart = Line > Span.Line || (Line == Span.Line && Column >= Span.Column);
    const bool BeforeEnd  = Line < Span.EndLine || (Line == Span.EndLine && Column <= Span.EndColumn);
    return AfterStart && BeforeEnd;
}

bool IsClassChar(char C) { return std::isalnum(static_cast<unsigned char>(C)) || C == '-' || C == '_'; }

// Calls Visit(Name, Line, Column) for each class name inside a `class="..."` or
// `class='...'` prop in Source, with its 1-based start.
template <typename Visit>
void ForEachChaosClass(std::string_view Source, Visit&& Each) {
    std::uint32_t Line = 1;
    std::uint32_t Column = 1;
    std::size_t   Pos = 0;
    auto          Step = [&] {
        if (Source[Pos] == '\n') {
            ++Line;
            Column = 1;
        } else {
            ++Column;
        }
        ++Pos;
    };
    constexpr std::string_view kProp = "class";
    while (Pos < Source.size()) {
        const bool AtProp = Source.compare(Pos, kProp.size(), kProp) == 0 &&
                            (Pos == 0 || !IsClassChar(Source[Pos - 1]));
        if (!AtProp) {
            Step();
            continue;
        }
        for (std::size_t I = 0; I < kProp.size(); ++I) {
            Step();
        }
        while (Pos < Source.size() && (Source[Pos] == ' ' || Source[Pos] == '\t')) {
            Step();
        }
        if (Pos >= Source.size() || Source[Pos] != '=') {
            continue;
        }
        Step();
        while (Pos < Source.size() && (Source[Pos] == ' ' || Source[Pos] == '\t')) {
            Step();
        }
        if (Pos >= Source.size() || (Source[Pos] != '"' && Source[Pos] != '\'')) {
            continue;
        }
        const char Quote = Source[Pos];
        Step();
        while (Pos < Source.size() && Source[Pos] != Quote && Source[Pos] != '\n') {
            if (!IsClassChar(Source[Pos])) {
                Step();
                continue;
            }
            const std::uint32_t StartLine = Line;
            const std::uint32_t StartColumn = Column;
            const std::size_t   Start = Pos;
            while (Pos < Source.size() && IsClassChar(Source[Pos])) {
                Step();
            }
            Each(Source.substr(Start, Pos - Start), StartLine, StartColumn);
        }
    }
}

} // namespace

std::optional<NamedSymbol> SymbolAt(std::string_view Source, std::uint32_t Line, std::uint32_t Column) {
    const std::vector<Cosmos::Token> Tokens = TokenizeDocument(Source);
    for (std::size_t I = 0; I < Tokens.size(); ++I) {
        const Cosmos::Token& T = Tokens[I];
        if (T.Kind == Cosmos::TokenKind::EndOfFile || !Contains(SpanOf(T), Line, Column)) {
            continue;
        }
        const bool FollowedByColon = I + 1 < Tokens.size() && Tokens[I + 1].Kind == Cosmos::TokenKind::Colon;
        switch (T.Kind) {
        case Cosmos::TokenKind::VariableName:
            return NamedSymbol{NamedSymbolKind::Variable, T.Text, SpanOf(T), FollowedByColon};
        case Cosmos::TokenKind::ClassName: return NamedSymbol{NamedSymbolKind::Class, T.Text, SpanOf(T), true};
        case Cosmos::TokenKind::Identifier:
            if (FollowedByColon) {
                return NamedSymbol{NamedSymbolKind::Property, T.Text, SpanOf(T), false};
            }
            break;
        default: break;
        }
    }
    return std::nullopt;
}

std::vector<Occurrence> VariableOccurrences(std::string_view Source, std::string_view Name) {
    const std::vector<Cosmos::Token> Tokens = TokenizeDocument(Source);
    std::vector<Occurrence>          Found;
    for (std::size_t I = 0; I < Tokens.size(); ++I) {
        if (Tokens[I].Kind == Cosmos::TokenKind::VariableName && Tokens[I].Text == Name) {
            const bool Declares = I + 1 < Tokens.size() && Tokens[I + 1].Kind == Cosmos::TokenKind::Colon;
            Found.push_back(Occurrence{SpanOf(Tokens[I]), Declares});
        }
    }
    return Found;
}

std::vector<Occurrence> ClassSelectorOccurrences(std::string_view Source, std::string_view Name) {
    std::vector<Occurrence> Found;
    for (const Cosmos::Token& T : TokenizeDocument(Source)) {
        if (T.Kind == Cosmos::TokenKind::ClassName && T.Text == Name) {
            Found.push_back(Occurrence{SpanOf(T), true});
        }
    }
    return Found;
}

std::vector<Occurrence> ClassUsesInChaos(std::string_view Source, std::string_view Name) {
    std::vector<Occurrence> Found;
    ForEachChaosClass(Source, [&](std::string_view Class, std::uint32_t Line, std::uint32_t Column) {
        if (Class == Name) {
            const auto End = Column + static_cast<std::uint32_t>(Class.size());
            Found.push_back(Occurrence{TextSpan{Line, Column, Line, End}, false});
        }
    });
    return Found;
}

std::optional<NamedSymbol> ChaosClassAt(std::string_view Source, std::uint32_t Line, std::uint32_t Column) {
    std::optional<NamedSymbol> Found;
    ForEachChaosClass(Source, [&](std::string_view Class, std::uint32_t AtLine, std::uint32_t AtColumn) {
        const TextSpan Span{AtLine, AtColumn, AtLine, AtColumn + static_cast<std::uint32_t>(Class.size())};
        if (!Found && Contains(Span, Line, Column)) {
            Found = NamedSymbol{NamedSymbolKind::Class, std::string(Class), Span, false};
        }
    });
    return Found;
}

std::string SelectorText(const Cosmos::Rule& R) {
    std::string Text = R.Kind == Cosmos::SelectorKind::Class ? "." + R.ClassName : R.PrimitiveName;
    if (R.Pseudo) {
        Text += R.Pseudo == Cosmos::PseudoKind::Hover    ? ":hover"
                : R.Pseudo == Cosmos::PseudoKind::Active ? ":active"
                                                         : ":disabled";
    }
    return Text;
}

} // namespace CosmosLsp
