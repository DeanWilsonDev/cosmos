#include "Cosmos/Resolver.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <functional>
#include <system_error>
#include <unordered_map>

namespace Cosmos {

struct TransparentStringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view Text) const { return std::hash<std::string_view>{}(Text); }
};

using RuleIndicesByName =
    std::unordered_map<std::string, std::vector<std::size_t>, TransparentStringHash, std::equal_to<>>;

struct SheetShape {
    std::size_t                RuleCount{0};
    const Rule*                FirstRule{nullptr};
    const Rule*                LastRule{nullptr};
    std::size_t                RootVariableCount{0};
    const VariableDeclaration* RootVariables{nullptr};

    bool operator==(const SheetShape&) const = default;
};

struct RuleIndex {
    SheetShape        Shape;
    RuleIndicesByName ByClass;
    RuleIndicesByName ByTag;
};

struct VariableScope {
    std::unordered_map<std::string, ValuePart> Values;
};

struct CachedVariableScope {
    std::shared_ptr<const RuleIndex> GlobalIndex;
    std::shared_ptr<const RuleIndex> ComponentIndex;
    VariableScope                    Scope;
};

namespace {

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
                                              "` -- only `width`, `height`, `min-width`, `max-width` and the "
                                              "offsets (`top`, `right`, `bottom`, `left`, `inset`) take `%`, "
                                              "`vw` or `vh`. Use a px length here."});
}

bool IsOffsetProperty(std::string_view Prop) {
    return Prop == "top" || Prop == "right" || Prop == "bottom" || Prop == "left";
}

// As in CSS, a negative margin pulls an element past its parent's content edge, and a
// negative offset moves it the other way. No other length can be negative.
bool TakesNegativeLength(std::string_view Prop) {
    return Prop == "margin" || Prop == "inset" || IsOffsetProperty(Prop);
}

std::optional<Offset>& OffsetField(std::string_view Prop, ResolvedStyle& Out) {
    if (Prop == "top") {
        return Out.Top;
    }
    if (Prop == "right") {
        return Out.Right;
    }
    if (Prop == "bottom") {
        return Out.Bottom;
    }
    return Out.Left;
}

// An offset is `auto` or a length in any unit; a percentage is of the containing block.
std::optional<Offset> ParseOffset(const ValuePart& Part) {
    if (Part.Literal == "auto") {
        return Offset{.Auto = true};
    }
    if (const std::optional<Length> L = ParseLength(Part)) {
        return Offset{.Auto = false, .Value = *L};
    }
    return std::nullopt;
}

// z-index is `auto` or a whole number, which may be negative, with no unit.
std::optional<StackLevel> ParseStackLevel(const ValuePart& Part) {
    if (Part.Literal == "auto") {
        return StackLevel{.Auto = true};
    }
    if (!Part.Number) {
        return std::nullopt;
    }
    const std::string& Text  = *Part.Number;
    int                Level = 0;
    const auto [End, Error]  = std::from_chars(Text.data(), Text.data() + Text.size(), Level);
    if (Error != std::errc{} || End != Text.data() + Text.size()) {
        return std::nullopt;
    }
    return StackLevel{.Auto = false, .Level = Level};
}

// A declaration as written, for a diagnostic: "`position: middle`".
std::string Written(const std::string& Prop, const std::vector<ValuePart>& Values) {
    std::string Text = "`" + Prop + ":";
    for (const ValuePart& V : Values) {
        Text += V.FollowsComma ? ", " : " ";
        if (V.Literal) {
            Text += *V.Literal;
            if (V.CallArgument) {
                Text += "(" + *V.CallArgument + ")";
            }
        } else if (V.Number) {
            Text += *V.Number;
        } else if (V.HexColor) {
            Text += *V.HexColor;
        } else if (V.StringValue) {
            Text += "\"" + *V.StringValue + "\"";
        } else if (V.AttrRef) {
            Text += "attr(" + *V.AttrRef + ")";
        } else if (V.ColorChannels) {
            Text += V.ColorChannels->size() == 4 ? "rgba(" : "rgb(";
            for (std::size_t I = 0; I < V.ColorChannels->size(); ++I) {
                const ColorChannelPart& Channel = (*V.ColorChannels)[I];
                Text += I == 0 ? "" : ", ";
                Text += Channel.AttrRef ? "attr(" + *Channel.AttrRef + ")" : Channel.Number.value_or("");
            }
            Text += ")";
        }
    }
    return Text + "`";
}

// A literal rgb()/rgba() channel: a whole number from 0 to 255, alpha included.
std::optional<std::uint8_t> ParseChannelNumber(const std::string& Text) {
    int        Value        = 0;
    const auto [End, Error] = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
    if (Error != std::errc{} || End != Text.data() + Text.size() || Value < 0 || Value > 255) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(Value);
}

bool BindsAnyChannel(const ValuePart& Part) {
    return Part.ColorChannels && std::ranges::any_of(*Part.ColorChannels, [](const ColorChannelPart& Channel) {
               return Channel.AttrRef.has_value();
           });
}

// An rgb() has no alpha channel, so it's opaque.
ColorChannel ResolveChannel(const std::vector<ColorChannelPart>& Channels, std::size_t Index) {
    if (Index >= Channels.size()) {
        return ColorChannel{.Value = 255};
    }
    const ColorChannelPart& Part = Channels[Index];
    if (Part.AttrRef) {
        return ColorChannel{.Attribute = *Part.AttrRef};
    }
    return ColorChannel{.Value = ParseChannelNumber(Part.Number.value_or("")).value_or(0)};
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

// A fraction from a plain number (0.3) or a percentage (30%), clamped to 0..1.
std::optional<float> ParseFraction(const ValuePart& Part) {
    if (!Part.Number) {
        return std::nullopt;
    }
    float             Value = 0.0F;
    const std::string Unit  = StripUnitSuffix(*Part.Number, Value);
    if (!Unit.empty() && Unit != "%") {
        return std::nullopt;
    }
    return std::clamp(Unit == "%" ? Value / 100.0F : Value, 0.0F, 1.0F);
}

// Degrees, written `3deg` or as a plain number.
std::optional<float> ParseDegrees(const ValuePart& Part) {
    if (!Part.Number) {
        return std::nullopt;
    }
    float             Value = 0.0F;
    const std::string Unit  = StripUnitSuffix(*Part.Number, Value);
    return Unit.empty() || Unit == "deg" ? std::optional<float>(Value) : std::nullopt;
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
    if (Part.ColorChannels) {
        if (BindsAnyChannel(Part)) {
            return std::nullopt;
        }
        const std::vector<ColorChannelPart>& Channels = *Part.ColorChannels;
        return Color{ResolveChannel(Channels, 0).Value, ResolveChannel(Channels, 1).Value,
                     ResolveChannel(Channels, 2).Value, ResolveChannel(Channels, 3).Value};
    }
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

// The single-number properties attr() can set, each with the field an earlier, concrete
// declaration of it would have filled in -- cleared when attr() takes the property over.
bool ClearAttrBindableProperty(const std::string& Prop, ResolvedStyle& Out) {
    if (Prop == "width") {
        Out.Width.reset();
    } else if (Prop == "height") {
        Out.Height.reset();
    } else if (Prop == "min-width") {
        Out.MinWidth.reset();
    } else if (Prop == "max-width") {
        Out.MaxWidth.reset();
    } else if (Prop == "flex-grow") {
        Out.FlexGrow.reset();
    } else if (Prop == "opacity") {
        Out.Opacity.reset();
    } else if (Prop == "gap") {
        Out.Gap.reset();
    } else if (Prop == "border-width") {
        Out.BorderWidth.reset();
    } else if (Prop == "border-radius") {
        Out.BorderRadius.reset();
    } else if (IsOffsetProperty(Prop)) {
        OffsetField(Prop, Out).reset();
    } else {
        return false;
    }
    return true;
}

// The colour properties an rgb()/rgba() with attr() channels can set, each with the field
// a concrete value of it fills in; null for any other property.
std::optional<Color>* ColorBindableField(std::string_view Prop, ResolvedStyle& Out) {
    if (Prop == "background-color") {
        return &Out.BackgroundColor;
    }
    if (Prop == "background-gradient-start") {
        return &Out.BackgroundGradientStart;
    }
    if (Prop == "background-gradient-end") {
        return &Out.BackgroundGradientEnd;
    }
    if (Prop == "border-color") {
        return &Out.BorderColor;
    }
    if (Prop == "color") {
        return &Out.TextColor;
    }
    return nullptr;
}

bool BindsColor(const ResolvedStyle& Style, std::string_view Prop) {
    return std::ranges::any_of(Style.ColorAttrBindings,
                               [&](const ColorAttrBinding& Binding) { return Binding.Property == Prop; });
}

bool IsContainerTag(std::string_view Tag) {
    static const std::unordered_map<std::string_view, bool> kIsContainer{
        {"Frame", true},  {"Grid", true},  {"Scroll", true}, {"Inline", true},
        {"Image", false}, {"Text", false}, {"Input", false}, {"TextArea", false},
        // A chart's children are its data, not boxes for it to lay out.
        {"BarChart", false}, {"LineChart", false}, {"DonutChart", false}, {"Series", false}, {"Slice", false}};
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

    // A declaration replaces any earlier attr() binding of the same property, as a later
    // value does in CSS, and an attr() replaces any earlier value.
    std::erase_if(Out.AttrBindings, [&](const AttrBinding& Binding) { return Binding.Property == Prop; });
    std::erase_if(Out.ColorAttrBindings, [&](const ColorAttrBinding& Binding) { return Binding.Property == Prop; });
    if (Resolved[0].AttrRef) {
        if (Resolved.size() != 1 || !ClearAttrBindableProperty(Prop, Out)) {
            const std::string ColorHint =
                ColorBindableField(Prop, Out) ? " A colour takes attr() per channel instead: "
                                                "`rgba(attr(r), attr(g), attr(b), attr(a))`."
                                              : "";
            Diagnostics.push_back(ResolveDiagnostic{"`" + Prop + ": attr(" + *Resolved[0].AttrRef +
                                                      ")` -- attr() sets a single number, and only `width`, "
                                                      "`height`, `min-width`, `max-width`, `flex-grow`, `opacity`, "
                                                      "`gap`, `border-width`, `border-radius`, `top`, `right`, "
                                                      "`bottom` and `left` take one." +
                                                      ColorHint});
            return;
        }
        Out.AttrBindings.push_back(AttrBinding{Prop, *Resolved[0].AttrRef});
        return;
    }

    for (const ValuePart& V : Resolved) {
        if (!V.ColorChannels) {
            continue;
        }
        for (const ColorChannelPart& Channel : *V.ColorChannels) {
            if (Channel.Number && !ParseChannelNumber(*Channel.Number)) {
                Diagnostics.push_back(ResolveDiagnostic{Written(Prop, Resolved) + " -- each channel of `rgb()` "
                                                        "and `rgba()` is a whole number from 0 to 255, alpha "
                                                        "included, or `attr(name)`."});
                return;
            }
        }
    }

    if (Prop == "box-shadow" && Resolved.size() == 2 && BindsAnyChannel(Resolved[0])) {
        if (const std::optional<float> Blur = ParsePixels(Resolved[1], Prop, Diagnostics)) {
            const std::vector<ColorChannelPart>& Channels = *Resolved[0].ColorChannels;
            Out.ShadowColor.reset();
            Out.ShadowBlurRadiusLogical = Blur;
            Out.ColorAttrBindings.push_back(ColorAttrBinding{Prop, ResolveChannel(Channels, 0),
                                                             ResolveChannel(Channels, 1), ResolveChannel(Channels, 2),
                                                             ResolveChannel(Channels, 3)});
            return;
        }
    }

    if (std::ranges::any_of(Resolved, BindsAnyChannel)) {
        std::optional<Color>* Field = Resolved.size() == 1 ? ColorBindableField(Prop, Out) : nullptr;
        if (!Field) {
            Diagnostics.push_back(ResolveDiagnostic{Written(Prop, Resolved) +
                                                      " -- attr() inside `rgb()`/`rgba()` sets only "
                                                      "`background-color`, `background-gradient-start`, "
                                                      "`background-gradient-end`, `border-color` and `color`, "
                                                      "each from a single colour, and `box-shadow`'s colour "
                                                      "before its blur radius."});
            return;
        }
        Field->reset();
        const std::vector<ColorChannelPart>& Channels = *Resolved[0].ColorChannels;
        Out.ColorAttrBindings.push_back(ColorAttrBinding{Prop, ResolveChannel(Channels, 0), ResolveChannel(Channels, 1),
                                                         ResolveChannel(Channels, 2), ResolveChannel(Channels, 3)});
        return;
    }

    // z-index isn't a length, and checks its own sign: it takes any integer.
    if (!TakesNegativeLength(Prop) && Prop != "z-index") {
        for (const auto& V : Resolved) {
            if (V.Number && V.Number->starts_with('-')) {
                Diagnostics.push_back(ResolveDiagnostic{"`" + Prop + ": " + *V.Number +
                                                          "` -- only `margin`, `top`, `right`, `bottom`, `left` "
                                                          "and `inset` take a negative length."});
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
    } else if (Prop == "selection-color") {
        Out.SelectionColor = ParseColor(Resolved[0]);
    } else if (Prop == "accent-color") {
        Out.AccentColor = ParseColor(Resolved[0]);
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
    } else if (Prop == "axis-color") {
        Out.Chart.AxisColor = ParseColor(Resolved[0]);
    } else if (Prop == "axis-width") {
        Out.Chart.AxisWidth = ParsePixels(Resolved[0], Prop, Diagnostics);
    } else if (Prop == "grid-color") {
        Out.Chart.GridColor = ParseColor(Resolved[0]);
    } else if (Prop == "grid-width") {
        Out.Chart.GridWidth = ParsePixels(Resolved[0], Prop, Diagnostics);
    } else if (Prop == "hover-color") {
        Out.Chart.HoverColor = ParseColor(Resolved[0]);
    } else if (Prop == "tick-spacing") {
        Out.Chart.TickSpacing = ParsePixels(Resolved[0], Prop, Diagnostics);
    } else if (Prop == "label-gap") {
        Out.Chart.LabelGap = ParsePixels(Resolved[0], Prop, Diagnostics);
    } else if (Prop == "line-width") {
        Out.Chart.LineWidth = ParsePixels(Resolved[0], Prop, Diagnostics);
    } else if (Prop == "point-radius") {
        Out.Chart.PointRadius = ParsePixels(Resolved[0], Prop, Diagnostics);
    } else if (Prop == "hover-point-radius") {
        Out.Chart.HoverPointRadius = ParsePixels(Resolved[0], Prop, Diagnostics);
    } else if (Prop == "area-opacity" || Prop == "bar-gap") {
        const std::optional<float> Fraction = ParseFraction(Resolved[0]);
        if (!Fraction) {
            Diagnostics.push_back(ResolveDiagnostic{"`" + Prop + "` takes a number from 0 to 1, or a percentage."});
            return;
        }
        (Prop == "area-opacity" ? Out.Chart.AreaOpacity : Out.Chart.BarGap) = Fraction;
    } else if (Prop == "bar-radius") {
        Out.Chart.BarRadius = ParsePixels(Resolved[0], Prop, Diagnostics);
    } else if (Prop == "donut-thickness") {
        Out.Chart.DonutThickness = ParsePixels(Resolved[0], Prop, Diagnostics);
    } else if (Prop == "segment-gap") {
        Out.Chart.SegmentGap = ParseDegrees(Resolved[0]);
        if (!Out.Chart.SegmentGap) {
            Diagnostics.push_back(ResolveDiagnostic{"`segment-gap` takes an angle, like `3deg`."});
        }
    } else if (Prop == "appear-duration") {
        Out.Chart.AppearSeconds = ParseSeconds(Resolved[0]);
    } else if (Prop == "change-duration") {
        Out.Chart.ChangeSeconds = ParseSeconds(Resolved[0]);
    } else if (Prop == "opacity") {
        float             Value = 0.0F;
        const std::string Unit  = Resolved[0].Number ? StripUnitSuffix(*Resolved[0].Number, Value) : std::string{};
        if (!Resolved[0].Number || (!Unit.empty() && Unit != "%")) {
            Diagnostics.push_back(ResolveDiagnostic{"`opacity` takes a number from 0 to 1, or a percentage."});
            return;
        }
        Out.Opacity = std::clamp(Unit == "%" ? Value / 100.0F : Value, 0.0F, 1.0F);
    } else if (Prop == "position") {
        static const std::unordered_map<std::string, Position> kPositions{{"static", Position::Static},
                                                                          {"relative", Position::Relative},
                                                                          {"absolute", Position::Absolute},
                                                                          {"fixed", Position::Fixed},
                                                                          {"sticky", Position::Sticky}};
        const auto It = Resolved[0].Literal ? kPositions.find(*Resolved[0].Literal) : kPositions.end();
        if (Resolved.size() != 1 || It == kPositions.end()) {
            Diagnostics.push_back(ResolveDiagnostic{Written(Prop, Resolved) +
                                                      " -- `position` takes `static`, `relative`, `absolute`, "
                                                      "`fixed` or `sticky`."});
            return;
        }
        Out.PositionMode = It->second;
    } else if (IsOffsetProperty(Prop)) {
        const std::optional<Offset> Parsed = Resolved.size() == 1 ? ParseOffset(Resolved[0]) : std::nullopt;
        if (!Parsed) {
            Diagnostics.push_back(
                ResolveDiagnostic{Written(Prop, Resolved) + " -- `" + Prop + "` takes `auto` or a length."});
            return;
        }
        OffsetField(Prop, Out) = Parsed;
    } else if (Prop == "inset") {
        std::vector<Offset> Parts;
        for (const auto& V : Resolved) {
            const std::optional<Offset> Parsed = ParseOffset(V);
            if (!Parsed) {
                Parts.clear();
                break;
            }
            Parts.push_back(*Parsed);
        }
        if (Parts.empty() || Parts.size() > 4) {
            Diagnostics.push_back(ResolveDiagnostic{Written(Prop, Resolved) +
                                                      " -- `inset` takes one to four values, each `auto` or a "
                                                      "length: top, right, bottom, left, as CSS's shorthand does."});
            return;
        }
        // CSS's one-to-four-value order, the same as margin's: a missing right copies top,
        // a missing bottom copies top, and a missing left copies right.
        Out.Top    = Parts[0];
        Out.Right  = Parts.size() > 1 ? Parts[1] : Parts[0];
        Out.Bottom = Parts.size() > 2 ? Parts[2] : Parts[0];
        Out.Left   = Parts.size() > 3 ? Parts[3] : *Out.Right;
        std::erase_if(Out.AttrBindings, [](const AttrBinding& Binding) { return IsOffsetProperty(Binding.Property); });
    } else if (Prop == "z-index") {
        const std::optional<StackLevel> Level = Resolved.size() == 1 ? ParseStackLevel(Resolved[0]) : std::nullopt;
        if (!Level) {
            Diagnostics.push_back(
                ResolveDiagnostic{Written(Prop, Resolved) + " -- `z-index` takes `auto` or an integer."});
            return;
        }
        Out.ZIndex = Level;
    } else if (Prop == "pointer-events") {
        if (Resolved.size() == 1 && Resolved[0].Literal == "auto") {
            Out.PointerEventsMode = PointerEvents::Auto;
        } else if (Resolved.size() == 1 && Resolved[0].Literal == "none") {
            Out.PointerEventsMode = PointerEvents::None;
        } else {
            Diagnostics.push_back(
                ResolveDiagnostic{Written(Prop, Resolved) + " -- `pointer-events` takes `auto` or `none`."});
            return;
        }
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

template <typename Visit>
void ForEachClassName(std::string_view ClassList, Visit&& Stop) {
    std::size_t Pos = 0;
    while (Pos < ClassList.size()) {
        while (Pos < ClassList.size() && std::isspace(static_cast<unsigned char>(ClassList[Pos]))) {
            ++Pos;
        }
        std::size_t End = Pos;
        while (End < ClassList.size() && !std::isspace(static_cast<unsigned char>(ClassList[End]))) {
            ++End;
        }
        if (End > Pos && Stop(ClassList.substr(Pos, End - Pos))) {
            return;
        }
        Pos = End;
    }
}

bool ClassListContains(std::string_view ClassList, std::string_view Name) {
    bool Found = false;
    ForEachClassName(ClassList, [&](std::string_view ClassName) { return Found = ClassName == Name; });
    return Found;
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

SheetShape ShapeOf(const Stylesheet& Sheet) {
    SheetShape Shape;
    Shape.RuleCount = Sheet.Rules.size();
    if (!Sheet.Rules.empty()) {
        Shape.FirstRule = Sheet.Rules.front().get();
        Shape.LastRule = Sheet.Rules.back().get();
    }
    if (Sheet.Root) {
        Shape.RootVariableCount = Sheet.Root->Variables.size();
        Shape.RootVariables = Sheet.Root->Variables.data();
    }
    return Shape;
}

const std::shared_ptr<const RuleIndex>& MatchIndexFor(const Stylesheet& Sheet) {
    const SheetShape Shape = ShapeOf(Sheet);
    if (Sheet.MatchIndex && Sheet.MatchIndex->Shape == Shape) {
        return Sheet.MatchIndex;
    }
    auto Index = std::make_shared<RuleIndex>();
    Index->Shape = Shape;
    for (std::size_t I = 0; I < Sheet.Rules.size(); ++I) {
        const Rule& R = *Sheet.Rules[I];
        if (R.Kind == SelectorKind::Class) {
            if (!R.ClassName.empty()) {
                Index->ByClass[R.ClassName].push_back(I);
            }
        } else if (const std::string_view Tag = PrimitiveTagForSelector(R.PrimitiveName); !Tag.empty()) {
            Index->ByTag[std::string(Tag)].push_back(I);
        }
    }
    Sheet.MatchIndex = std::move(Index);
    return Sheet.MatchIndex;
}

std::shared_ptr<const CachedVariableScope> VariableScopeFor(const StylesheetSet& Sheets) {
    const Stylesheet* Owner = Sheets.Component ? Sheets.Component : Sheets.Global;
    if (!Owner) {
        static const std::shared_ptr<const CachedVariableScope> Empty = std::make_shared<CachedVariableScope>();
        return Empty;
    }
    std::shared_ptr<const RuleIndex> GlobalIndex = Sheets.Global ? MatchIndexFor(*Sheets.Global) : nullptr;
    std::shared_ptr<const RuleIndex> ComponentIndex = Sheets.Component ? MatchIndexFor(*Sheets.Component) : nullptr;
    const std::shared_ptr<const CachedVariableScope>& Cached = Owner->VariableScopeCache;
    if (Cached && Cached->GlobalIndex == GlobalIndex && Cached->ComponentIndex == ComponentIndex) {
        return Cached;
    }
    auto Fresh = std::make_shared<CachedVariableScope>();
    Fresh->GlobalIndex = std::move(GlobalIndex);
    Fresh->ComponentIndex = std::move(ComponentIndex);
    Fresh->Scope = BuildVariableScope(Sheets.Global, Sheets.Component);
    Owner->VariableScopeCache = Fresh;
    return Fresh;
}

void AddIndexed(const RuleIndicesByName& Map, std::string_view Name, std::vector<std::size_t>& Out) {
    const auto It = Map.find(Name);
    if (It != Map.end()) {
        Out.insert(Out.end(), It->second.begin(), It->second.end());
    }
}

std::vector<std::size_t> CandidateRules(const RuleIndex& Index, const std::vector<const IStyleTarget*>& Chain) {
    std::vector<std::size_t> Candidates;
    for (const IStyleTarget* Element : Chain) {
        const std::string Classes = Element->ClassName();
        ForEachClassName(Classes, [&](std::string_view ClassName) {
            AddIndexed(Index.ByClass, ClassName, Candidates);
            return false;
        });
        AddIndexed(Index.ByTag, Element->PrimitiveTag(), Candidates);
    }
    std::sort(Candidates.begin(), Candidates.end());
    Candidates.erase(std::unique(Candidates.begin(), Candidates.end()), Candidates.end());
    return Candidates;
}

void ApplyLayer(const Stylesheet* Sheet, const IStyleTarget& Target, bool Unbounded, const VariableScope& Scope,
                 ResolvedStyle& Out, std::vector<ResolveDiagnostic>& Diagnostics) {
    if (!Sheet) {
        return;
    }
    const std::vector<const IStyleTarget*> Chain = BuildAncestorChain(Target, Unbounded);

    std::vector<MatchedRule> Matches;
    for (const std::size_t RuleIndexInSheet : CandidateRules(*MatchIndexFor(*Sheet), Chain)) {
        CollectMatches(*Sheet->Rules[RuleIndexInSheet], Chain, Chain.size(), Matches);
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

bool IsGradientStop(std::string_view Prop) {
    return Prop == "background-gradient-start" || Prop == "background-gradient-end";
}

void FinalizePairedProperties(ResolvedStyle& Style) {
    const bool HasStart = Style.BackgroundGradientStart || BindsColor(Style, "background-gradient-start");
    const bool HasEnd   = Style.BackgroundGradientEnd || BindsColor(Style, "background-gradient-end");
    if (!HasStart || !HasEnd) {
        Style.BackgroundGradientStart.reset();
        Style.BackgroundGradientEnd.reset();
        std::erase_if(Style.ColorAttrBindings,
                      [](const ColorAttrBinding& Binding) { return IsGradientStop(Binding.Property); });
    }
    const bool HasShadowColor = Style.ShadowColor || BindsColor(Style, "box-shadow");
    if (!HasShadowColor || !Style.ShadowBlurRadiusLogical.has_value()) {
        Style.ShadowColor.reset();
        Style.ShadowBlurRadiusLogical.reset();
        std::erase_if(Style.ColorAttrBindings,
                      [](const ColorAttrBinding& Binding) { return Binding.Property == "box-shadow"; });
    }
    CombineFont(Style);
    for (const std::shared_ptr<ResolvedStyle>& Overlay : {Style.Hover, Style.Active, Style.Disabled}) {
        if (Overlay) CombineFont(*Overlay);
    }
}

ResolvedStyle ResolveCascadedLayers(const IStyleTarget& Target, const StylesheetSet& Sheets,
                                     std::vector<ResolveDiagnostic>& Diagnostics) {
    const std::shared_ptr<const CachedVariableScope> Scope = VariableScopeFor(Sheets);

    ResolvedStyle Style;
    ApplyLayer(Sheets.Global, Target, true, Scope->Scope, Style, Diagnostics);
    ApplyLayer(Sheets.Component, Target, false, Scope->Scope, Style, Diagnostics);
    FinalizePairedProperties(Style);
    return Style;
}

} // namespace

ResolvedStyle ResolveStyle(const IStyleTarget& Target, const StylesheetSet& Sheets,
                            std::vector<ResolveDiagnostic>& OutDiagnostics) {
    ResolvedStyle Style = ResolveCascadedLayers(Target, Sheets, OutDiagnostics);

    // A bound colour is the element's own, so it inherits nothing; its value is only known
    // to the backend, so its descendants inherit nothing from it either.
    const bool InheritsColor = !Style.TextColor && !BindsColor(Style, "color");
    if ((InheritsColor || !Style.FontFamily || !Style.FontSizeLogical || !Style.FontStyleMode ||
         !Style.PointerEventsMode) &&
        Target.Parent() != nullptr) {
        const ResolvedStyle Ancestor = ResolveStyle(*Target.Parent(), Sheets, OutDiagnostics);
        if (InheritsColor) {
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
        if (!Style.PointerEventsMode) {
            Style.PointerEventsMode = Ancestor.PointerEventsMode;
        }
        CombineFont(Style);
    }

    return Style;
}

std::string_view PrimitiveTagForSelector(std::string_view CosmosSelectorName) {
    static const std::unordered_map<std::string_view, std::string_view> kMapping{
        {"frame", "Frame"}, {"inline", "Inline"}, {"grid", "Grid"},   {"image", "Image"},
        {"text", "Text"},   {"scroll", "Scroll"}, {"input", "Input"}, {"textarea", "TextArea"},
        {"barchart", "BarChart"}, {"linechart", "LineChart"}, {"donutchart", "DonutChart"},
        {"series", "Series"},     {"slice", "Slice"}};
    auto It = kMapping.find(CosmosSelectorName);
    return It == kMapping.end() ? std::string_view{} : It->second;
}

ResolvedStyle Resolver::Resolve(const IStyleTarget& Target, const StylesheetSet& Sheets, bool Unbounded,
                                 std::vector<ResolveDiagnostic>& OutDiagnostics) const {
    const std::shared_ptr<const CachedVariableScope> Scope = VariableScopeFor(Sheets);

    ResolvedStyle Style;
    ApplyLayer(Sheets.Global, Target, Unbounded, Scope->Scope, Style, OutDiagnostics);
    ApplyLayer(Sheets.Component, Target, Unbounded, Scope->Scope, Style, OutDiagnostics);
    FinalizePairedProperties(Style);
    return Style;
}

} // namespace Cosmos
