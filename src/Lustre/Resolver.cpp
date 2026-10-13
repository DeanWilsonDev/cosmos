#include "Lustre/Resolver.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <unordered_map>

namespace Lustre {

namespace {

struct VariableScope {
    std::unordered_map<std::string, ValuePart> Values;
};

void CollectVariablesFromRules(const std::vector<RulePtr>& Rules, std::unordered_map<std::string, ValuePart>& Out) {
    for (const auto& R : Rules) {
        for (const auto& V : R->Variables) {
            Out[V.Name] = V.Value;
        }
        CollectVariablesFromRules(R->NestedRules, Out);
    }
}

// §1.4: a component-scoped --variable shadows a same-named global one,
// within that component's own file only.
VariableScope BuildVariableScope(const Stylesheet* Global, const Stylesheet* Component) {
    VariableScope Scope;
    if (Global && Global->Root) {
        for (const auto& V : Global->Root->Variables) {
            Scope.Values[V.Name] = V.Value;
        }
    }
    if (Component) {
        if (Component->Root) {
            for (const auto& V : Component->Root->Variables) {
                Scope.Values[V.Name] = V.Value;
            }
        }
        CollectVariablesFromRules(Component->Rules, Scope.Values);
    }
    return Scope;
}

// Resolves a var(--name) reference through the scope. Returns nullptr (and
// records a diagnostic) if the referenced variable was never declared --
// §1.4/§6: no fallback form exists, an undefined reference is always an
// error.
const ValuePart* ResolveVariableRef(const ValuePart& Part, const VariableScope& Scope,
                                     std::vector<ResolveDiagnostic>& Diagnostics) {
    if (!Part.VariableRef) {
        return &Part;
    }
    auto It = Scope.Values.find(*Part.VariableRef);
    if (It == Scope.Values.end()) {
        Diagnostics.push_back(ResolveDiagnostic{"Undefined variable `var(--" + *Part.VariableRef + ")` -- no `--" +
                                                  *Part.VariableRef + "` is declared globally or in this component's scope."});
        return nullptr;
    }
    // A variable's own value can't itself be another var() reference in any
    // example the spec gives; if it somehow is, resolve one level deep and
    // stop there rather than looping.
    if (It->second.VariableRef) {
        return ResolveVariableRef(It->second, Scope, Diagnostics);
    }
    return &It->second;
}

std::string StripUnitSuffix(std::string_view Text, float& OutValue) {
    std::size_t End = Text.size();
    while (End > 0 && !(std::isdigit(static_cast<unsigned char>(Text[End - 1])))) {
        --End;
    }
    const std::string_view NumericPart = Text.substr(0, End);
    OutValue = 0.0F;
    std::from_chars(NumericPart.data(), NumericPart.data() + NumericPart.size(), OutValue);
    return std::string(Text.substr(End));
}

std::optional<Length> ParseLength(const ValuePart& Part) {
    if (!Part.Number) {
        return std::nullopt;
    }
    float             Value = 0.0F;
    const std::string Unit  = StripUnitSuffix(*Part.Number, Value);
    if (Unit == "%") {
        return Length{Value, LengthUnit::Percent};
    }
    if (Unit == "vw") {
        return Length{Value, LengthUnit::Vw};
    }
    if (Unit == "vh") {
        return Length{Value, LengthUnit::Vh};
    }
    return Length{Value, LengthUnit::Px};
}

bool IsRelative(const Length& L) { return L.Unit != LengthUnit::Px; }

void ReportRelativeLength(const std::string& Prop, const ValuePart& Part, std::vector<ResolveDiagnostic>& Diagnostics) {
    Diagnostics.push_back(ResolveDiagnostic{"`" + Prop + ": " + *Part.Number +
                                              "` -- only `width`, `height`, `min-width` and `max-width` take `%`, "
                                              "`vw` or `vh`. Use a px length here."});
}

std::optional<float> ParsePixels(const ValuePart& Part, const std::string& Prop,
                                 std::vector<ResolveDiagnostic>& Diagnostics) {
    const std::optional<Length> Parsed = ParseLength(Part);
    if (!Parsed) {
        return std::nullopt;
    }
    if (IsRelative(*Parsed)) {
        ReportRelativeLength(Prop, Part, Diagnostics);
        return std::nullopt;
    }
    return Parsed->Value;
}

std::optional<float> ParseSeconds(const ValuePart& Part) {
    if (!Part.Number) {
        return std::nullopt;
    }
    float             Value = 0.0F;
    const std::string Unit  = StripUnitSuffix(*Part.Number, Value);
    return Unit == "ms" ? Value / 1000.0F : Value;
}

std::optional<Easing> ParseEasing(std::string_view Keyword) {
    static const std::unordered_map<std::string_view, Easing> kEasings{{"linear", Easing::Linear},
                                                                       {"ease", Easing::Ease},
                                                                       {"ease-in", Easing::EaseIn},
                                                                       {"ease-out", Easing::EaseOut},
                                                                       {"ease-in-out", Easing::EaseInOut}};
    auto It = kEasings.find(Keyword);
    return It == kEasings.end() ? std::nullopt : std::optional<Easing>(It->second);
}

std::optional<Color> ParseColor(const ValuePart& Part) {
    if (!Part.HexColor) {
        return std::nullopt;
    }
    std::string_view Hex = *Part.HexColor;
    if (!Hex.empty() && Hex.front() == '#') {
        Hex.remove_prefix(1);
    }
    auto HexPair = [](std::string_view S, std::size_t Index) -> std::uint8_t {
        if (Index + 1 >= S.size()) {
            return 0;
        }
        unsigned Value = 0;
        std::from_chars(S.data() + Index, S.data() + Index + 2, Value, 16);
        return static_cast<std::uint8_t>(Value);
    };
    auto HexNibble = [](std::string_view S, std::size_t Index) -> std::uint8_t {
        if (Index >= S.size()) {
            return 0;
        }
        unsigned Value = 0;
        std::from_chars(S.data() + Index, S.data() + Index + 1, Value, 16);
        return static_cast<std::uint8_t>(Value * 17); // nibble-doubled, e.g. 'E' -> 0xEE
    };
    Color C;
    if (Hex.size() == 3) {
        C.R = HexNibble(Hex, 0);
        C.G = HexNibble(Hex, 1);
        C.B = HexNibble(Hex, 2);
        C.A = 255;
    } else if (Hex.size() == 6) {
        C.R = HexPair(Hex, 0);
        C.G = HexPair(Hex, 2);
        C.B = HexPair(Hex, 4);
        C.A = 255;
    } else if (Hex.size() == 8) {
        C.R = HexPair(Hex, 0);
        C.G = HexPair(Hex, 2);
        C.B = HexPair(Hex, 4);
        C.A = HexPair(Hex, 6);
    } else {
        return std::nullopt;
    }
    return C;
}

std::optional<EdgeInsets> ParseEdgeInsets(const std::vector<float>& Lengths) {
    // CSS 1-4 value shorthand (§2's `padding`/`margin`).
    switch (Lengths.size()) {
    case 1: return EdgeInsets{Lengths[0], Lengths[0], Lengths[0], Lengths[0]};
    case 2: return EdgeInsets{Lengths[1], Lengths[0], Lengths[1], Lengths[0]};
    case 3: return EdgeInsets{Lengths[1], Lengths[0], Lengths[1], Lengths[2]};
    case 4: return EdgeInsets{Lengths[3], Lengths[0], Lengths[1], Lengths[2]};
    default: return std::nullopt;
    }
}

bool IsContainerTag(std::string_view Tag) {
    static const std::unordered_map<std::string_view, bool> kIsContainer{
        {"Frame", true},  {"Grid", true},  {"Scroll", true}, {"Inline", true},
        {"Image", false}, {"Text", false}, {"Input", false}, {"TextArea", false}};
    auto It = kIsContainer.find(Tag);
    return It == kIsContainer.end() || It->second;
}

bool IsContainerOnlyProperty(const std::string& Prop) {
    return Prop == "display" || Prop == "flex-direction" || Prop == "gap" || Prop == "align-items" ||
           Prop == "justify-content";
}

void ApplyDeclaration(const Declaration& Decl, const VariableScope& Scope, ResolvedStyle& Out,
                       std::string_view TargetTag, std::vector<ResolveDiagnostic>& Diagnostics) {
    std::vector<ValuePart> Resolved;
    Resolved.reserve(Decl.Values.size());
    for (const auto& Raw : Decl.Values) {
        if (const ValuePart* R = ResolveVariableRef(Raw, Scope, Diagnostics)) {
            Resolved.push_back(*R);
            Resolved.back().FollowsComma = Raw.FollowsComma; // where the comma was, not the variable's
        }
    }
    if (Resolved.empty()) {
        return;
    }

    const std::string& Prop = Decl.Property;

    if (Prop != "margin") {
        for (const auto& V : Resolved) {
            if (V.Number && V.Number->starts_with('-')) {
                Diagnostics.push_back(ResolveDiagnostic{"`" + Prop + ": " + *V.Number +
                                                          "` -- only `margin` takes a negative length."});
                return;
            }
        }
    }

    if (IsContainerOnlyProperty(Prop) && !TargetTag.empty() && !IsContainerTag(TargetTag)) {
        Diagnostics.push_back(ResolveDiagnostic{"`" + Prop + "` has no effect on `" + std::string(TargetTag) +
                                                  "` -- it only applies to containers (`Frame`, `Grid`, `Scroll`, " +
                                                  "`Inline`), and " + std::string(TargetTag) +
                                                  " is a leaf with no children to lay out."});
        return;
    }

    if (Prop == "background-color") {
        Out.BackgroundColor = ParseColor(Resolved[0]);
    } else if (Prop == "background-gradient-start") {
        Out.BackgroundGradientStart = ParseColor(Resolved[0]);
    } else if (Prop == "background-gradient-end") {
        Out.BackgroundGradientEnd = ParseColor(Resolved[0]);
    } else if (Prop == "box-shadow") {
        std::optional<Color> ShadowColor;
        std::optional<float> ShadowBlur;
        for (const auto& V : Resolved) {
            if (auto C = ParseColor(V)) {
                ShadowColor = C;
            } else if (auto L = ParsePixels(V, Prop, Diagnostics)) {
                ShadowBlur = L;
            }
        }
        Out.ShadowColor = ShadowColor;
        Out.ShadowBlurRadiusLogical = ShadowBlur;
    } else if (Prop == "border-color") {
        Out.BorderColor = ParseColor(Resolved[0]);
    } else if (Prop == "border-width") {
        Out.BorderWidth = ParsePixels(Resolved[0], Prop, Diagnostics);
    } else if (Prop == "border-radius") {
        Out.BorderRadius = ParsePixels(Resolved[0], Prop, Diagnostics);
    } else if (Prop == "padding" || Prop == "margin") {
        std::vector<float> Lengths;
        for (const auto& V : Resolved) {
            const std::optional<Length> L = ParseLength(V);
            if (!L) {
                continue;
            }
            if (IsRelative(*L)) {
                ReportRelativeLength(Prop, V, Diagnostics);
                return;
            }
            Lengths.push_back(L->Value);
        }
        (Prop == "padding" ? Out.Padding : Out.Margin) = ParseEdgeInsets(Lengths);
    } else if (Prop == "color") {
        Out.TextColor = ParseColor(Resolved[0]);
    } else if (Prop == "font-family") {
        if (Resolved[0].StringValue) {
            Out.FontFamily = Resolved[0].StringValue;
        }
    } else if (Prop == "font-size") {
        Out.FontSizeLogical = ParsePixels(Resolved[0], Prop, Diagnostics);
    } else if (Prop == "display") {
        if (Resolved[0].Literal == "stack") {
            Out.DisplayMode = Display::Stack;
        } else if (Resolved[0].Literal == "inline") {
            Out.DisplayMode = Display::Inline;
        }
    } else if (Prop == "flex-direction") {
        if (Resolved[0].Literal == "row") {
            Out.FlexDirectionMode = FlexDirection::Row;
        } else if (Resolved[0].Literal == "column") {
            Out.FlexDirectionMode = FlexDirection::Column;
        }
    } else if (Prop == "gap") {
        Out.Gap = ParsePixels(Resolved[0], Prop, Diagnostics);
    } else if (Prop == "align-items") {
        static const std::unordered_map<std::string, Align> kAligns{
            {"start", Align::Start}, {"center", Align::Center}, {"end", Align::End}, {"stretch", Align::Stretch}};
        if (Resolved[0].Literal) {
            auto It = kAligns.find(*Resolved[0].Literal);
            if (It != kAligns.end()) {
                Out.AlignItems = It->second;
            }
        }
    } else if (Prop == "justify-content") {
        static const std::unordered_map<std::string, Justify> kJustify{
            {"start", Justify::Start},
            {"center", Justify::Center},
            {"end", Justify::End},
            {"space-between", Justify::SpaceBetween}};
        if (Resolved[0].Literal) {
            auto It = kJustify.find(*Resolved[0].Literal);
            if (It != kJustify.end()) {
                Out.JustifyContent = It->second;
            }
        }
    } else if (Prop == "width") {
        Out.Width = ParseLength(Resolved[0]);
    } else if (Prop == "height") {
        Out.Height = ParseLength(Resolved[0]);
    } else if (Prop == "min-width") {
        Out.MinWidth = ParseLength(Resolved[0]);
    } else if (Prop == "max-width") {
        Out.MaxWidth = ParseLength(Resolved[0]);
    } else if (Prop == "text-overflow") {
        if (Resolved[0].Literal == "clip") {
            Out.TextOverflowMode = TextOverflow::Clip;
        } else if (Resolved[0].Literal == "ellipsis") {
            Out.TextOverflowMode = TextOverflow::Ellipsis;
        }
    } else if (Prop == "white-space") {
        if (Resolved[0].Literal == "nowrap") {
            Out.WhiteSpaceMode = WhiteSpace::Nowrap;
        } else if (Resolved[0].Literal == "normal") {
            Out.WhiteSpaceMode = WhiteSpace::Normal;
        }
    } else if (Prop == "font-style") {
        if (Resolved[0].Literal == "italic") {
            Out.FontStyleMode = FontStyle::Italic;
        } else if (Resolved[0].Literal == "normal") {
            Out.FontStyleMode = FontStyle::Normal;
        }
    } else if (Prop == "text-decoration") {
        TextDecoration Decoration;
        bool           Recognized = false;
        for (const auto& V : Resolved) {
            if (V.Literal == "underline") {
                Decoration.Underline = true;
                Recognized = true;
            } else if (V.Literal == "line-through") {
                Decoration.LineThrough = true;
                Recognized = true;
            } else if (V.Literal == "none") {
                Recognized = true;
            }
        }
        if (Recognized) {
            Out.TextDecorationLine = Decoration;
        }
    } else if (Prop == "flex-grow") {
        Out.FlexGrow = ParsePixels(Resolved[0], Prop, Diagnostics);
    } else if (Prop == "scrollbar-color") {
        Out.ScrollbarThumbColor = ParseColor(Resolved[0]);
        if (Resolved.size() > 1) {
            Out.ScrollbarTrackColor = ParseColor(Resolved[1]);
        }
    } else if (Prop == "scrollbar-width") {
        Out.ScrollbarWidthLogical = ParsePixels(Resolved[0], Prop, Diagnostics);
    } else if (Prop == "transform") {
        if (Resolved[0].Literal == "scale" && Resolved[0].CallArgument) {
            float Value = 0.0F;
            std::from_chars(Resolved[0].CallArgument->data(),
                             Resolved[0].CallArgument->data() + Resolved[0].CallArgument->size(), Value);
            Out.TransformScale = Value;
        }
    } else if (Prop == "transition") {
        // CSS's shorthand without a delay: a comma-separated list of
        // `<property> <duration> [<easing>]`, in any order within an entry.
        std::vector<Transition> List;
        for (const auto& V : Resolved) {
            if (List.empty() || V.FollowsComma) {
                List.emplace_back();
            }
            Transition& Entry = List.back();
            if (V.Literal) {
                if (const std::optional<Easing> Curve = ParseEasing(*V.Literal)) {
                    Entry.Curve = *Curve;
                } else {
                    Entry.Property = *V.Literal;
                }
            } else if (const std::optional<float> Seconds = ParseSeconds(V)) {
                Entry.DurationSeconds = *Seconds;
            }
        }
        std::erase_if(List, [](const Transition& Entry) { return Entry.Property.empty(); });
        Out.Transitions = std::move(List);
    } else if (Prop == "opacity") {
        float             Value = 0.0F;
        const std::string Unit  = Resolved[0].Number ? StripUnitSuffix(*Resolved[0].Number, Value) : std::string{};
        if (!Resolved[0].Number || (!Unit.empty() && Unit != "%")) {
            Diagnostics.push_back(ResolveDiagnostic{"`opacity` takes a number from 0 to 1, or a percentage."});
            return;
        }
        Out.Opacity = std::clamp(Unit == "%" ? Value / 100.0F : Value, 0.0F, 1.0F);
    }
    // Unknown properties are silently ignored -- v1 does no
    // property-applicability validation (§6, "Deliberately not diagnosed").
}

ResolvedStyle& OverlayFor(ResolvedStyle& Base, PseudoKind Kind) {
    std::shared_ptr<ResolvedStyle>* Slot = nullptr;
    switch (Kind) {
    case PseudoKind::Hover: Slot = &Base.Hover; break;
    case PseudoKind::Active: Slot = &Base.Active; break;
    case PseudoKind::Disabled: Slot = &Base.Disabled; break;
    }
    if (!*Slot) {
        *Slot = std::make_shared<ResolvedStyle>();
    }
    return **Slot;
}

void ApplyRuleDeclarations(const Rule& R, const VariableScope& Scope, ResolvedStyle& Out, std::string_view TargetTag,
                            std::vector<ResolveDiagnostic>& Diagnostics) {
    ResolvedStyle& Target = R.Pseudo ? OverlayFor(Out, *R.Pseudo) : Out;
    for (const auto& Decl : R.Declarations) {
        ApplyDeclaration(Decl, Scope, Target, TargetTag, Diagnostics);
    }

    if (R.StartingDeclarations.empty()) {
        return;
    }
    if (R.Pseudo) {
        // An element appears once, not once per interaction state.
        Diagnostics.push_back(ResolveDiagnostic{"`@starting-style` has no effect inside a `:" +
                                                  std::string(R.Pseudo == PseudoKind::Hover    ? "hover"
                                                              : R.Pseudo == PseudoKind::Active ? "active"
                                                                                               : "disabled") +
                                                  "` rule -- put it in the rule for the element itself."});
        return;
    }
    if (!Out.Starting) {
        Out.Starting = std::make_shared<ResolvedStyle>();
    }
    for (const auto& Decl : R.StartingDeclarations) {
        ApplyDeclaration(Decl, Scope, *Out.Starting, TargetTag, Diagnostics);
    }
}

bool ClassListContains(std::string_view ClassList, std::string_view Name) {
    std::size_t Pos = 0;
    while (Pos < ClassList.size()) {
        while (Pos < ClassList.size() && std::isspace(static_cast<unsigned char>(ClassList[Pos]))) {
            ++Pos;
        }
        std::size_t End = Pos;
        while (End < ClassList.size() && !std::isspace(static_cast<unsigned char>(ClassList[End]))) {
            ++End;
        }
        if (End > Pos && ClassList.substr(Pos, End - Pos) == Name) {
            return true;
        }
        Pos = End;
    }
    return false;
}

bool SelectorHeadMatches(const Rule& R, const IStyleTarget& Element) {
    if (R.Kind == SelectorKind::Class) {
        return !R.ClassName.empty() && ClassListContains(Element.ClassName(), R.ClassName);
    }
    const std::string_view MappedTag = PrimitiveTagForSelector(R.PrimitiveName);
    return !MappedTag.empty() && Element.PrimitiveTag() == MappedTag;
}

struct MatchedRule {
    const Rule* R;
};

// Builds the boundary-respecting ancestor chain for descendant matching
// (§1.2): index 0 is Target itself, each following index is one step
// further up. The walk stops (excludes anything past it) at the first node
// where IsComponentRoot() is true, unless Unbounded (global.lustre has no
// "own subtree" to be bounded by).
std::vector<const IStyleTarget*> BuildAncestorChain(const IStyleTarget& Target, bool Unbounded) {
    std::vector<const IStyleTarget*> Chain{&Target};
    if (Target.IsComponentRoot() && !Unbounded) {
        return Chain;
    }
    const IStyleTarget* Node = Target.Parent();
    while (Node) {
        Chain.push_back(Node);
        if (Node->IsComponentRoot() && !Unbounded) {
            break;
        }
        Node = Node->Parent();
    }
    return Chain;
}

// Walks one rule (and its nested descendant selectors) against the ancestor
// chain, collecting every (Rule) whose full selector chain matches Target
// (chain[0]). BoundExclusive restricts how far up the chain this node's own
// selector may match -- strictly closer to the target than whatever
// ancestor its own lexical parent selector matched, giving real descendant
// semantics ("at any depth" below that ancestor, never above it).
void CollectMatches(const Rule& Node, const std::vector<const IStyleTarget*>& Chain, std::size_t BoundExclusive,
                     std::vector<MatchedRule>& Out) {
    for (std::size_t I = 0; I < BoundExclusive && I < Chain.size(); ++I) {
        if (!SelectorHeadMatches(Node, *Chain[I])) {
            continue;
        }
        if (I == 0) {
            Out.push_back(MatchedRule{&Node});
        }
        for (const auto& Child : Node.NestedRules) {
            CollectMatches(*Child, Chain, I, Out);
        }
    }
}

void ApplyLayer(const Stylesheet* Sheet, const IStyleTarget& Target, bool Unbounded, const VariableScope& Scope,
                 ResolvedStyle& Out, std::vector<ResolveDiagnostic>& Diagnostics) {
    if (!Sheet) {
        return;
    }
    const std::vector<const IStyleTarget*> Chain = BuildAncestorChain(Target, Unbounded);

    std::vector<MatchedRule> Matches;
    for (const auto& TopRule : Sheet->Rules) {
        CollectMatches(*TopRule, Chain, Chain.size(), Matches);
    }

    std::stable_sort(Matches.begin(), Matches.end(), [](const MatchedRule& A, const MatchedRule& B) {
        if (A.R->Depth != B.R->Depth) {
            return A.R->Depth < B.R->Depth;
        }
        return A.R->SourceIndex < B.R->SourceIndex;
    });

    const std::string TargetTag = Target.PrimitiveTag();
    for (const auto& Match : Matches) {
        ApplyRuleDeclarations(*Match.R, Scope, Out, TargetTag, Diagnostics);
    }
}

void CombineFont(ResolvedStyle& Style) {
    if (Style.FontFamily && Style.FontSizeLogical) {
        Style.Font = FontRequest{*Style.FontFamily, *Style.FontSizeLogical};
    }
}

void FinalizePairedProperties(ResolvedStyle& Style) {
    if (!Style.BackgroundGradientStart.has_value() || !Style.BackgroundGradientEnd.has_value()) {
        Style.BackgroundGradientStart.reset();
        Style.BackgroundGradientEnd.reset();
    }
    if (!Style.ShadowColor.has_value() || !Style.ShadowBlurRadiusLogical.has_value()) {
        Style.ShadowColor.reset();
        Style.ShadowBlurRadiusLogical.reset();
    }
    CombineFont(Style);
    for (const std::shared_ptr<ResolvedStyle>& Overlay : {Style.Hover, Style.Active, Style.Disabled}) {
        if (Overlay) CombineFont(*Overlay);
    }
}

// §1.3's two-layer cascade, composed correctly in one call: global.lustre Unbounded (it has
// no "own subtree" to be bounded by), the component's own file bounded to Target's own
// component root. Resolver::Resolve() alone can't express differing Unbounded per layer in
// a single call, so this applies each layer's own rules directly (mirroring Resolve()'s own
// body, just with a per-layer Unbounded) instead of routing through it.
//
// BUG FIXED 2026-08-03 (docs/next_steps.md): this used to call Resolver::Resolve() twice,
// once per layer, each time handing it a StylesheetSet with only *that* layer's Stylesheet*
// populated (the other left nullptr), then merge the two independently-produced
// ResolvedStyles. Resolve() builds its VariableScope from whichever Sheets it's actually
// given (line ~536 below) -- so that isolated the variable scope per layer too, not just
// which sheet's selectors got matched. A component-layer declaration referencing a variable
// declared only in global.lustre's own :root (the entire point of §1.3/§1.4's two-layer
// split) silently failed to resolve: the ResolveVariableRef lookup missed, the whole
// declaration was dropped, and the property came back unset -- not wrong, just as if never
// written. Fixed by building the VariableScope once from the *full*, un-isolated Sheets
// (both members, exactly as the caller passed them in) and reusing it for both layers' own
// rule-application passes -- a variable's visibility is a property of the whole cascade
// (§1.4: "a component-scoped --variable shadows a same-named global one"), not of which
// single sheet's selectors a given pass happens to be matching. This also means the two
// layers now write directly onto one shared ResolvedStyle in cascade order (Global then
// Component, matching Resolve()'s own priority), so MergeCascadeInto's separate-then-merge
// step is no longer needed at all.
ResolvedStyle ResolveCascadedLayers(const IStyleTarget& Target, const StylesheetSet& Sheets,
                                     std::vector<ResolveDiagnostic>& Diagnostics) {
    const VariableScope Scope = BuildVariableScope(Sheets.Global, Sheets.Component);

    ResolvedStyle Style;
    ApplyLayer(Sheets.Global, Target, /*Unbounded=*/true, Scope, Style, Diagnostics);
    ApplyLayer(Sheets.Component, Target, /*Unbounded=*/false, Scope, Style, Diagnostics);
    FinalizePairedProperties(Style);
    return Style;
}

} // namespace

ResolvedStyle ResolveStyle(const IStyleTarget& Target, const StylesheetSet& Sheets,
                            std::vector<ResolveDiagnostic>& OutDiagnostics) {
    ResolvedStyle Style = ResolveCascadedLayers(Target, Sheets, OutDiagnostics);

    if ((!Style.TextColor || !Style.FontFamily || !Style.FontSizeLogical || !Style.FontStyleMode) &&
        Target.Parent() != nullptr) {
        const ResolvedStyle Ancestor = ResolveStyle(*Target.Parent(), Sheets, OutDiagnostics);
        if (!Style.TextColor) {
            Style.TextColor = Ancestor.TextColor;
        }
        if (!Style.FontFamily) {
            Style.FontFamily = Ancestor.FontFamily;
        }
        if (!Style.FontSizeLogical) {
            Style.FontSizeLogical = Ancestor.FontSizeLogical;
        }
        if (!Style.FontStyleMode) {
            Style.FontStyleMode = Ancestor.FontStyleMode;
        }
        CombineFont(Style);
    }

    return Style;
}

std::string_view PrimitiveTagForSelector(std::string_view LustreSelectorName) {
    static const std::unordered_map<std::string_view, std::string_view> kMapping{
        {"frame", "Frame"}, {"inline", "Inline"}, {"grid", "Grid"},   {"image", "Image"},
        {"text", "Text"},   {"scroll", "Scroll"}, {"input", "Input"}, {"textarea", "TextArea"}};
    auto It = kMapping.find(LustreSelectorName);
    return It == kMapping.end() ? std::string_view{} : It->second;
}

ResolvedStyle Resolver::Resolve(const IStyleTarget& Target, const StylesheetSet& Sheets, bool Unbounded,
                                 std::vector<ResolveDiagnostic>& OutDiagnostics) const {
    const VariableScope Scope = BuildVariableScope(Sheets.Global, Sheets.Component);

    ResolvedStyle Style;
    // §1.3: exactly two cascade layers -- global.lustre first, then the
    // component's own file overrides it for anything both define. Applied
    // as two fully independent passes (not merged by specificity across
    // layers) so the component layer always wins regardless of nesting
    // depth on either side.
    ApplyLayer(Sheets.Global, Target, Unbounded, Scope, Style, OutDiagnostics);
    ApplyLayer(Sheets.Component, Target, Unbounded, Scope, Style, OutDiagnostics);
    FinalizePairedProperties(Style);
    return Style;
}

} // namespace Lustre
