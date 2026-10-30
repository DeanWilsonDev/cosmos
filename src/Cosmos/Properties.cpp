#include "Cosmos/Properties.h"

#include <algorithm>
#include <array>

namespace Cosmos {

namespace {

constexpr std::array<std::string_view, 2> kDisplay{"stack", "inline"};
constexpr std::array<std::string_view, 2> kFlexDirection{"row", "column"};
constexpr std::array<std::string_view, 4> kAlignItems{"start", "center", "end", "stretch"};
constexpr std::array<std::string_view, 4> kJustifyContent{"start", "center", "end", "space-between"};
constexpr std::array<std::string_view, 2> kTextOverflow{"clip", "ellipsis"};
constexpr std::array<std::string_view, 2> kWhiteSpace{"nowrap", "normal"};
constexpr std::array<std::string_view, 2> kFontStyle{"normal", "italic"};
constexpr std::array<std::string_view, 3> kTextDecoration{"none", "underline", "line-through"};
constexpr std::array<std::string_view, 5> kEasings{"linear", "ease", "ease-in", "ease-out", "ease-in-out"};
constexpr std::array<std::string_view, 5> kPosition{"static", "relative", "absolute", "fixed", "sticky"};
constexpr std::array<std::string_view, 2> kPointerEvents{"auto", "none"};
constexpr std::array<std::string_view, 1> kAuto{"auto"};
constexpr std::array<std::string_view, 1> kScale{"scale"};

using Shape = PropertyValueShape;

constexpr PropertyInfo Make(std::string_view Name, Shape ValueShape, std::string_view Summary,
                            std::span<const std::string_view> Keywords = {}, bool ContainerOnly = false,
                            bool Inherited = false) {
    return PropertyInfo{Name, ValueShape, Keywords, ContainerOnly, Inherited, Summary};
}

constexpr std::array kProperties{
    Make("background-color", Shape::Color, "Fills the element's box."),
    Make("background-gradient-start", Shape::Color,
         "The top colour of a vertical two-stop gradient. Needs `background-gradient-end` too."),
    Make("background-gradient-end", Shape::Color,
         "The bottom colour of a vertical two-stop gradient. Needs `background-gradient-start` too."),
    Make("box-shadow", Shape::Shadow, "A shadow around the box: a colour and a blur radius."),
    Make("border-color", Shape::Color, "The border's colour."),
    Make("border-width", Shape::Length, "The border's thickness."),
    Make("border-radius", Shape::Length, "Rounds every corner by one radius."),
    Make("padding", Shape::EdgeLengths, "Space inside the border."),
    Make("margin", Shape::EdgeLengths, "Space outside the border."),
    Make("color", Shape::Color, "The text colour.", {}, false, true),
    Make("font-family", Shape::FontPath, "The font file text is drawn with.", {}, false, true),
    Make("font-size", Shape::Length, "The text size.", {}, false, true),
    Make("display", Shape::Keyword, "How a container lays out its children: `stack` or `inline`.", kDisplay, true),
    Make("flex-direction", Shape::Keyword, "A stack's main axis.", kFlexDirection, true),
    Make("gap", Shape::Length, "The space between a container's children.", {}, true),
    Make("align-items", Shape::Keyword, "Where a container's children sit on its cross axis.", kAlignItems, true),
    Make("justify-content", Shape::Keyword, "How a container spreads its children along its main axis.",
         kJustifyContent, true),
    Make("width", Shape::Size, "The border-box width."),
    Make("height", Shape::Size, "The border-box height."),
    Make("min-width", Shape::Size, "The least width. Wins over `max-width`."),
    Make("max-width", Shape::Size, "The greatest width. On `text`, the line width `text-overflow` clips at."),
    Make("text-overflow", Shape::Keyword, "What single-line text past `max-width` does.", kTextOverflow),
    Make("white-space", Shape::Keyword, "`normal` wraps text across lines; `nowrap` keeps it on one.",
         kWhiteSpace),
    Make("font-style", Shape::Keyword, "Upright or italic text.", kFontStyle, false, true),
    Make("text-decoration", Shape::KeywordList, "Lines drawn with the text.", kTextDecoration),
    Make("transform", Shape::Scale, "Scales the element. Accepted, but no backend applies it yet.", kScale),
    Make("transition", Shape::Transition, "Eases changes to colour properties over a duration.", kEasings),
    Make("flex-grow", Shape::Number, "The element's share of its stack's spare main-axis space."),
    Make("scrollbar-color", Shape::OneOrTwoColors, "The scrollbar's thumb colour, then its track colour."),
    Make("scrollbar-width", Shape::Length, "The scrollbar thumb's thickness."),
    Make("selection-color", Shape::Color, "The highlight behind selected text."),
    Make("accent-color", Shape::Color, "A checkbox's checked fill, or a select's highlighted row."),
    Make("opacity", Shape::Fraction, "How opaque the element is."),
    Make("axis-color", Shape::Color, "A chart's baseline colour."),
    Make("axis-width", Shape::Length, "A chart's baseline thickness."),
    Make("grid-color", Shape::Color, "A chart's grid line colour, one line per value tick."),
    Make("grid-width", Shape::Length, "A chart's grid line thickness."),
    Make("hover-color", Shape::Color, "Drawn behind or over a chart's hovered entry."),
    Make("tick-spacing", Shape::Length, "The least distance between a chart's value ticks."),
    Make("label-gap", Shape::Length, "The space between a chart and its labels."),
    Make("line-width", Shape::Length, "A line chart's line thickness."),
    Make("point-radius", Shape::Length, "A line chart's point radius."),
    Make("hover-point-radius", Shape::Length, "A line chart's hovered point radius."),
    Make("area-opacity", Shape::Fraction, "The opacity of the fill under a line chart's line."),
    Make("bar-gap", Shape::Fraction, "The share of a bar chart's category band left empty."),
    Make("bar-radius", Shape::Length, "A bar's corner radius."),
    Make("donut-thickness", Shape::Length, "A donut chart's ring thickness; 0 is a pie."),
    Make("segment-gap", Shape::Angle, "The angle between a donut chart's slices."),
    Make("appear-duration", Shape::Time, "How long a chart takes to grow in when shown."),
    Make("change-duration", Shape::Time, "How long a chart takes to ease to new data."),
    Make("position", Shape::Keyword, "How the element is placed, as in CSS.", kPosition),
    Make("top", Shape::Offset, "The offset from the containing block's top edge.", kAuto),
    Make("right", Shape::Offset, "The offset from the containing block's right edge.", kAuto),
    Make("bottom", Shape::Offset, "The offset from the containing block's bottom edge.", kAuto),
    Make("left", Shape::Offset, "The offset from the containing block's left edge.", kAuto),
    Make("inset", Shape::Offsets, "Sets `top`, `right`, `bottom` and `left` at once.", kAuto),
    Make("z-index", Shape::StackLevel, "Orders the element within its stacking context.", kAuto),
    Make("pointer-events", Shape::Keyword, "`none` lets the pointer through to what's underneath.", kPointerEvents,
         false, true),
};

constexpr std::array kPseudoClasses{
    PseudoClassInfo{"hover", "While the pointer is over the element."},
    PseudoClassInfo{"active", "While the element is pressed."},
    PseudoClassInfo{"disabled", "While the element is disabled."},
};

std::string QuotedChoice(std::span<const std::string_view> Words) {
    std::string Text;
    for (std::size_t I = 0; I < Words.size(); ++I) {
        if (I > 0) {
            Text += I + 1 == Words.size() ? " or " : ", ";
        }
        Text += "`";
        Text += Words[I];
        Text += "`";
    }
    return Text;
}

std::size_t EditDistance(std::string_view A, std::string_view B) {
    std::vector<std::size_t> Row(B.size() + 1);
    for (std::size_t J = 0; J <= B.size(); ++J) {
        Row[J] = J;
    }
    for (std::size_t I = 1; I <= A.size(); ++I) {
        std::size_t Diagonal = Row[0];
        Row[0]               = I;
        for (std::size_t J = 1; J <= B.size(); ++J) {
            const std::size_t Above = Row[J];
            Row[J] = std::min({Row[J] + 1, Row[J - 1] + 1, Diagonal + (A[I - 1] == B[J - 1] ? 0U : 1U)});
            Diagonal = Above;
        }
    }
    return Row[B.size()];
}

} // namespace

std::span<const PropertyInfo> AllProperties() { return kProperties; }

const PropertyInfo* FindProperty(std::string_view Name) {
    const auto It = std::ranges::find(kProperties, Name, &PropertyInfo::Name);
    return It == kProperties.end() ? nullptr : &*It;
}

std::span<const PseudoClassInfo> AllPseudoClasses() { return kPseudoClasses; }

std::vector<std::string_view> PropertyNames() {
    std::vector<std::string_view> Names;
    for (const PropertyInfo& Property : kProperties) {
        Names.push_back(Property.Name);
    }
    return Names;
}

std::string DescribeValues(const PropertyInfo& Property) {
    switch (Property.Shape) {
    case Shape::Color: return "a colour: a hex literal like `#4a90ff`, or `rgb()` or `rgba()`";
    case Shape::OneOrTwoColors: return "one or two colours: the thumb's, then the track's";
    case Shape::Length: return "a length in `px`, like `8px`";
    case Shape::EdgeLengths: return "one to four lengths in `px`: top, right, bottom, left, as in CSS";
    case Shape::Size: return "a length in `px`, `%`, `vw` or `vh`";
    case Shape::Number: return "a number, like `1`";
    case Shape::Keyword: return QuotedChoice(Property.Keywords);
    case Shape::KeywordList: return "`none`, or any of " + QuotedChoice(Property.Keywords.subspan(1));
    case Shape::FontPath: return "a font file's path in quotes, like `\"assets/fonts/body.ttf\"`";
    case Shape::Time: return "a duration, like `0.2s` or `150ms`";
    case Shape::Fraction: return "a number from 0 to 1, or a percentage";
    case Shape::Angle: return "an angle, like `3deg`";
    case Shape::Offset: return "`auto` or a length";
    case Shape::Offsets: return "one to four values, each `auto` or a length: top, right, bottom, left";
    case Shape::StackLevel: return "`auto` or an integer";
    case Shape::Scale: return "`scale(<number>)`";
    case Shape::Transition: return "a property, a duration and an optional easing, comma-separated";
    case Shape::Shadow: return "a colour and a blur radius in `px`";
    }
    return "";
}

std::string_view ClosestMatch(std::string_view Name, std::span<const std::string_view> Candidates) {
    const std::size_t Allowed = std::clamp<std::size_t>(Name.size() / 3, 1, 3);
    std::string_view  Best;
    std::size_t       BestDistance = Allowed + 1;
    for (const std::string_view Candidate : Candidates) {
        const std::size_t Distance = EditDistance(Name, Candidate);
        if (Distance > 0 && Distance < BestDistance) {
            Best         = Candidate;
            BestDistance = Distance;
        }
    }
    return Best;
}

} // namespace Cosmos
