#include "Cosmos/Explain.h"

#include "Cosmos/Properties.h"
#include "ResolverDetail.h"

#include <algorithm>
#include <format>
#include <functional>
#include <optional>
#include <set>

namespace Cosmos {

namespace {

StyleState StateOf(const Rule& R) {
    if (!R.Pseudo) {
        return StyleState::Base;
    }
    switch (*R.Pseudo) {
    case PseudoKind::Hover: return StyleState::Hover;
    case PseudoKind::Active: return StyleState::Active;
    case PseudoKind::Disabled: return StyleState::Disabled;
    }
    return StyleState::Base;
}

std::vector<std::string> PropertiesSetBy(const std::string& Property) {
    if (Property == "inset") {
        return {"top", "right", "bottom", "left"};
    }
    return {Property};
}

std::string ResolvedText(const Declaration& Decl, const VariableScope& Scope) {
    if (std::ranges::none_of(Decl.Values, [](const ValuePart& Part) { return Part.VariableRef.has_value(); })) {
        return "";
    }
    std::vector<ValuePart> Resolved;
    for (const ValuePart& Part : Decl.Values) {
        if (!Part.VariableRef) {
            Resolved.push_back(Part);
            continue;
        }
        const Detail::VariableLookup Lookup = Detail::LookUpVariable(Part, Scope);
        if (!Lookup.Value) {
            return "";
        }
        ValuePart Followed    = *Lookup.Value;
        Followed.FollowsComma = Part.FollowsComma;
        Resolved.push_back(std::move(Followed));
    }
    return Detail::WrittenValues(Resolved);
}

std::string ProblemWith(const Declaration& Decl, const VariableScope& Scope, std::string_view Tag) {
    if (!FindProperty(Decl.Property)) {
        return "Unknown property `" + Decl.Property + "` -- Cosmos ignores it.";
    }
    std::vector<ResolveDiagnostic> Found;
    Detail::ApplyOneDeclaration(Decl, Scope, Tag, Found);
    return Found.empty() ? "" : Found.front().Message;
}

void ExplainLayer(const Stylesheet* Sheet, bool FromGlobal, const IStyleTarget& Target, const VariableScope& Scope,
                  std::vector<ExplainedRule>& Out) {
    if (!Sheet) {
        return;
    }
    const std::string Tag = Target.PrimitiveTag();
    for (std::vector<const Rule*>& Lineage : Detail::MatchingRuleChains(*Sheet, Target, FromGlobal)) {
        ExplainedRule Explained{std::move(Lineage), FromGlobal, {}};
        const Rule&   R = *Explained.Lineage.back();
        auto          Add = [&](const Declaration& Decl, StyleState State) {
            Explained.Declarations.push_back(
                ExplainedDeclaration{&Decl, State, Decl.Property + ": " + Detail::WrittenValues(Decl.Values),
                                     ResolvedText(Decl, Scope), ProblemWith(Decl, Scope, Tag), nullptr});
        };
        for (const Declaration& Decl : R.Declarations) {
            Add(Decl, StateOf(R));
        }
        if (!R.Pseudo) {
            for (const Declaration& Decl : R.StartingDeclarations) {
                Add(Decl, StyleState::Starting);
            }
        }
        Out.push_back(std::move(Explained));
    }
}

void MarkOverridden(std::vector<ExplainedRule>& Rules) {
    std::vector<ExplainedDeclaration*> InOrder;
    for (ExplainedRule& R : Rules) {
        for (ExplainedDeclaration& D : R.Declarations) {
            InOrder.push_back(&D);
        }
    }
    for (std::size_t I = 0; I < InOrder.size(); ++I) {
        ExplainedDeclaration&          Earlier = *InOrder[I];
        const std::vector<std::string> Needed  = PropertiesSetBy(Earlier.Decl->Property);
        std::set<std::string>          Covered;
        for (std::size_t J = I + 1; J < InOrder.size(); ++J) {
            const ExplainedDeclaration& Later = *InOrder[J];
            if (Later.State != Earlier.State || !Later.Problem.empty()) {
                continue;
            }
            for (const std::string& Property : PropertiesSetBy(Later.Decl->Property)) {
                Covered.insert(Property);
            }
            if (std::ranges::all_of(Needed, [&](const std::string& P) { return Covered.contains(P); })) {
                Earlier.OverriddenBy = Later.Decl;
                break;
            }
        }
    }
}

std::string Number(float Value) { return std::format("{}", Value); }
std::string Pixels(float Value) { return Number(Value) + "px"; }
std::string Seconds(float Value) { return Number(Value) + "s"; }

std::string ColorText(const Color& C) {
    return C.A == 255 ? std::format("#{:02x}{:02x}{:02x}", C.R, C.G, C.B)
                      : std::format("#{:02x}{:02x}{:02x}{:02x}", C.R, C.G, C.B, C.A);
}

std::string LengthText(const Length& L) {
    switch (L.Unit) {
    case LengthUnit::Px: return Number(L.Value) + "px";
    case LengthUnit::Percent: return Number(L.Value) + "%";
    case LengthUnit::Vw: return Number(L.Value) + "vw";
    case LengthUnit::Vh: return Number(L.Value) + "vh";
    }
    return Number(L.Value);
}

std::string EdgesText(const EdgeInsets& E) {
    if (E.Top == E.Bottom && E.Left == E.Right) {
        return E.Top == E.Left ? Pixels(E.Top) : Pixels(E.Top) + " " + Pixels(E.Left);
    }
    return Pixels(E.Top) + " " + Pixels(E.Right) + " " + Pixels(E.Bottom) + " " + Pixels(E.Left);
}

std::string OffsetText(const Offset& O) { return O.Auto ? "auto" : LengthText(O.Value); }

std::string EasingText(Easing Curve) {
    switch (Curve) {
    case Easing::Linear: return "linear";
    case Easing::Ease: return "ease";
    case Easing::EaseIn: return "ease-in";
    case Easing::EaseOut: return "ease-out";
    case Easing::EaseInOut: return "ease-in-out";
    }
    return "ease";
}

std::string ChannelText(const ColorChannel& Channel) {
    return Channel.Attribute ? "attr(" + *Channel.Attribute + ")" : std::to_string(Channel.Value);
}

template <typename T, typename Format>
std::optional<std::string> TextOf(const std::optional<T>& Value, Format&& Text) {
    return Value ? std::optional<std::string>(Text(*Value)) : std::nullopt;
}

std::optional<std::string> ValueOf(const ResolvedStyle& S, std::string_view Name) {
    for (const AttrBinding& Binding : S.AttrBindings) {
        if (Binding.Property == Name) {
            return "attr(" + Binding.Attribute + ")";
        }
    }
    for (const ColorAttrBinding& Binding : S.ColorAttrBindings) {
        if (Binding.Property == Name) {
            return "rgba(" + ChannelText(Binding.R) + ", " + ChannelText(Binding.G) + ", " + ChannelText(Binding.B) +
                   ", " + ChannelText(Binding.A) + ")";
        }
    }
    static const std::vector<std::pair<std::string_view, std::function<std::optional<std::string>(const ResolvedStyle&)>>>
        kReaders{
            {"background-color", [](const ResolvedStyle& S) { return TextOf(S.BackgroundColor, ColorText); }},
            {"background-gradient-start",
             [](const ResolvedStyle& S) { return TextOf(S.BackgroundGradientStart, ColorText); }},
            {"background-gradient-end",
             [](const ResolvedStyle& S) { return TextOf(S.BackgroundGradientEnd, ColorText); }},
            {"box-shadow",
             [](const ResolvedStyle& S) -> std::optional<std::string> {
                 if (!S.ShadowColor || !S.ShadowBlurRadiusLogical) {
                     return std::nullopt;
                 }
                 return ColorText(*S.ShadowColor) + " " + Pixels(*S.ShadowBlurRadiusLogical);
             }},
            {"border-color", [](const ResolvedStyle& S) { return TextOf(S.BorderColor, ColorText); }},
            {"border-width", [](const ResolvedStyle& S) { return TextOf(S.BorderWidth, Pixels); }},
            {"border-radius", [](const ResolvedStyle& S) { return TextOf(S.BorderRadius, Pixels); }},
            {"padding", [](const ResolvedStyle& S) { return TextOf(S.Padding, EdgesText); }},
            {"margin", [](const ResolvedStyle& S) { return TextOf(S.Margin, EdgesText); }},
            {"color", [](const ResolvedStyle& S) { return TextOf(S.TextColor, ColorText); }},
            {"font-family",
             [](const ResolvedStyle& S) {
                 return TextOf(S.FontFamily, [](const std::string& Path) { return "\"" + Path + "\""; });
             }},
            {"font-size", [](const ResolvedStyle& S) { return TextOf(S.FontSizeLogical, Pixels); }},
            {"display",
             [](const ResolvedStyle& S) {
                 return TextOf(S.DisplayMode,
                               [](Display D) { return std::string(D == Display::Stack ? "stack" : "inline"); });
             }},
            {"flex-direction",
             [](const ResolvedStyle& S) {
                 return TextOf(S.FlexDirectionMode,
                               [](FlexDirection D) { return std::string(D == FlexDirection::Row ? "row" : "column"); });
             }},
            {"gap", [](const ResolvedStyle& S) { return TextOf(S.Gap, Pixels); }},
            {"align-items",
             [](const ResolvedStyle& S) {
                 return TextOf(S.AlignItems, [](Align A) {
                     switch (A) {
                     case Align::Start: return std::string("start");
                     case Align::Center: return std::string("center");
                     case Align::End: return std::string("end");
                     case Align::Stretch: return std::string("stretch");
                     }
                     return std::string();
                 });
             }},
            {"justify-content",
             [](const ResolvedStyle& S) {
                 return TextOf(S.JustifyContent, [](Justify J) {
                     switch (J) {
                     case Justify::Start: return std::string("start");
                     case Justify::Center: return std::string("center");
                     case Justify::End: return std::string("end");
                     case Justify::SpaceBetween: return std::string("space-between");
                     }
                     return std::string();
                 });
             }},
            {"width", [](const ResolvedStyle& S) { return TextOf(S.Width, LengthText); }},
            {"height", [](const ResolvedStyle& S) { return TextOf(S.Height, LengthText); }},
            {"min-width", [](const ResolvedStyle& S) { return TextOf(S.MinWidth, LengthText); }},
            {"max-width", [](const ResolvedStyle& S) { return TextOf(S.MaxWidth, LengthText); }},
            {"text-overflow",
             [](const ResolvedStyle& S) {
                 return TextOf(S.TextOverflowMode, [](TextOverflow T) {
                     return std::string(T == TextOverflow::Clip ? "clip" : "ellipsis");
                 });
             }},
            {"white-space",
             [](const ResolvedStyle& S) {
                 return TextOf(S.WhiteSpaceMode,
                               [](WhiteSpace W) { return std::string(W == WhiteSpace::Nowrap ? "nowrap" : "normal"); });
             }},
            {"font-style",
             [](const ResolvedStyle& S) {
                 return TextOf(S.FontStyleMode,
                               [](FontStyle F) { return std::string(F == FontStyle::Normal ? "normal" : "italic"); });
             }},
            {"text-decoration",
             [](const ResolvedStyle& S) {
                 return TextOf(S.TextDecorationLine, [](const TextDecoration& D) {
                     if (D.Underline && D.LineThrough) {
                         return std::string("underline line-through");
                     }
                     return std::string(D.Underline ? "underline" : D.LineThrough ? "line-through" : "none");
                 });
             }},
            {"transform",
             [](const ResolvedStyle& S) {
                 return TextOf(S.TransformScale, [](float Scale) { return "scale(" + Number(Scale) + ")"; });
             }},
            {"transition",
             [](const ResolvedStyle& S) -> std::optional<std::string> {
                 if (S.Transitions.empty()) {
                     return std::nullopt;
                 }
                 std::string Text;
                 for (const Transition& T : S.Transitions) {
                     Text += (Text.empty() ? "" : ", ") + T.Property + " " + Seconds(T.DurationSeconds) + " " +
                             EasingText(T.Curve);
                 }
                 return Text;
             }},
            {"flex-grow", [](const ResolvedStyle& S) { return TextOf(S.FlexGrow, Number); }},
            {"scrollbar-color",
             [](const ResolvedStyle& S) -> std::optional<std::string> {
                 if (!S.ScrollbarThumbColor) {
                     return std::nullopt;
                 }
                 return ColorText(*S.ScrollbarThumbColor) +
                        (S.ScrollbarTrackColor ? " " + ColorText(*S.ScrollbarTrackColor) : "");
             }},
            {"scrollbar-width", [](const ResolvedStyle& S) { return TextOf(S.ScrollbarWidthLogical, Pixels); }},
            {"selection-color", [](const ResolvedStyle& S) { return TextOf(S.SelectionColor, ColorText); }},
            {"accent-color", [](const ResolvedStyle& S) { return TextOf(S.AccentColor, ColorText); }},
            {"opacity", [](const ResolvedStyle& S) { return TextOf(S.Opacity, Number); }},
            {"axis-color", [](const ResolvedStyle& S) { return TextOf(S.Chart.AxisColor, ColorText); }},
            {"axis-width", [](const ResolvedStyle& S) { return TextOf(S.Chart.AxisWidth, Pixels); }},
            {"grid-color", [](const ResolvedStyle& S) { return TextOf(S.Chart.GridColor, ColorText); }},
            {"grid-width", [](const ResolvedStyle& S) { return TextOf(S.Chart.GridWidth, Pixels); }},
            {"hover-color", [](const ResolvedStyle& S) { return TextOf(S.Chart.HoverColor, ColorText); }},
            {"tick-spacing", [](const ResolvedStyle& S) { return TextOf(S.Chart.TickSpacing, Pixels); }},
            {"label-gap", [](const ResolvedStyle& S) { return TextOf(S.Chart.LabelGap, Pixels); }},
            {"line-width", [](const ResolvedStyle& S) { return TextOf(S.Chart.LineWidth, Pixels); }},
            {"point-radius", [](const ResolvedStyle& S) { return TextOf(S.Chart.PointRadius, Pixels); }},
            {"hover-point-radius", [](const ResolvedStyle& S) { return TextOf(S.Chart.HoverPointRadius, Pixels); }},
            {"area-opacity", [](const ResolvedStyle& S) { return TextOf(S.Chart.AreaOpacity, Number); }},
            {"bar-gap", [](const ResolvedStyle& S) { return TextOf(S.Chart.BarGap, Number); }},
            {"bar-radius", [](const ResolvedStyle& S) { return TextOf(S.Chart.BarRadius, Pixels); }},
            {"donut-thickness", [](const ResolvedStyle& S) { return TextOf(S.Chart.DonutThickness, Pixels); }},
            {"segment-gap",
             [](const ResolvedStyle& S) {
                 return TextOf(S.Chart.SegmentGap, [](float Degrees) { return Number(Degrees) + "deg"; });
             }},
            {"appear-duration", [](const ResolvedStyle& S) { return TextOf(S.Chart.AppearSeconds, Seconds); }},
            {"change-duration", [](const ResolvedStyle& S) { return TextOf(S.Chart.ChangeSeconds, Seconds); }},
            {"position",
             [](const ResolvedStyle& S) {
                 return TextOf(S.PositionMode, [](Position P) {
                     switch (P) {
                     case Position::Static: return std::string("static");
                     case Position::Relative: return std::string("relative");
                     case Position::Absolute: return std::string("absolute");
                     case Position::Fixed: return std::string("fixed");
                     case Position::Sticky: return std::string("sticky");
                     }
                     return std::string();
                 });
             }},
            {"top", [](const ResolvedStyle& S) { return TextOf(S.Top, OffsetText); }},
            {"right", [](const ResolvedStyle& S) { return TextOf(S.Right, OffsetText); }},
            {"bottom", [](const ResolvedStyle& S) { return TextOf(S.Bottom, OffsetText); }},
            {"left", [](const ResolvedStyle& S) { return TextOf(S.Left, OffsetText); }},
            {"z-index",
             [](const ResolvedStyle& S) {
                 return TextOf(S.ZIndex,
                               [](const StackLevel& L) { return L.Auto ? std::string("auto") : std::to_string(L.Level); });
             }},
            {"pointer-events",
             [](const ResolvedStyle& S) {
                 return TextOf(S.PointerEventsMode,
                               [](PointerEvents P) { return std::string(P == PointerEvents::Auto ? "auto" : "none"); });
             }},
        };
    const auto Reader = std::ranges::find(kReaders, Name, [](const auto& Entry) { return Entry.first; });
    return Reader == kReaders.end() ? std::nullopt : Reader->second(S);
}

} // namespace

StyleExplanation ExplainStyle(const IStyleTarget& Target, const StylesheetSet& Sheets) {
    StyleExplanation    Explanation;
    const VariableScope Scope = Detail::BuildScope(Sheets);
    ExplainLayer(Sheets.Global, true, Target, Scope, Explanation.Rules);
    ExplainLayer(Sheets.Component, false, Target, Scope, Explanation.Rules);
    MarkOverridden(Explanation.Rules);

    Explanation.Style = ResolveStyle(Target, Sheets, Explanation.Diagnostics);
    std::vector<ResolveDiagnostic> Ignored;
    const ResolvedStyle            Own = Detail::ResolveOwnStyle(Target, Sheets, Ignored);
    for (const StyleProperty& Property : DescribeStyle(Explanation.Style)) {
        if (!ValueOf(Own, Property.Name)) {
            Explanation.InheritedProperties.push_back(Property.Name);
        }
    }

    std::vector<ResolveDiagnostic> Unique;
    for (ResolveDiagnostic& Diagnostic : Explanation.Diagnostics) {
        const bool Seen = std::ranges::any_of(Unique, [&](const ResolveDiagnostic& Kept) {
            return Kept.Message == Diagnostic.Message && Kept.Location.Line == Diagnostic.Location.Line &&
                   Kept.Location.Column == Diagnostic.Location.Column &&
                   Kept.Location.FilePath == Diagnostic.Location.FilePath;
        });
        if (!Seen) {
            Unique.push_back(std::move(Diagnostic));
        }
    }
    Explanation.Diagnostics = std::move(Unique);
    return Explanation;
}

std::string SelectorChainText(const std::vector<const Rule*>& Lineage) {
    std::string Text;
    for (const Rule* R : Lineage) {
        if (!Text.empty()) {
            Text += " ";
        }
        Text += R->Kind == SelectorKind::Class ? "." + R->ClassName : R->PrimitiveName;
        if (R->Pseudo) {
            Text += *R->Pseudo == PseudoKind::Hover    ? ":hover"
                    : *R->Pseudo == PseudoKind::Active ? ":active"
                                                       : ":disabled";
        }
    }
    return Text;
}

std::vector<StyleProperty> DescribeStyle(const ResolvedStyle& Style) {
    std::vector<StyleProperty> Properties;
    for (const PropertyInfo& Info : AllProperties()) {
        if (std::optional<std::string> Value = ValueOf(Style, Info.Name)) {
            Properties.push_back(StyleProperty{std::string(Info.Name), std::move(*Value)});
        }
    }
    return Properties;
}

} // namespace Cosmos
