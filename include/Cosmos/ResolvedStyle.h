#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Cosmos {

// docs/cosmos_core_spec.md §3 — the resolver's output. Backend-agnostic:
// carries every property in §2 (real or stubbed), no Penumbra (or any other
// backend) type anywhere in here. A bridge repo (`iris-penumbra-backend` for
// Penumbra) maps this onto concrete style structs.

struct Color {
    std::uint8_t R{0};
    std::uint8_t G{0};
    std::uint8_t B{0};
    std::uint8_t A{255};

    friend bool operator==(const Color&, const Color&) = default;
};

struct EdgeInsets {
    float Left{0.0F};
    float Top{0.0F};
    float Right{0.0F};
    float Bottom{0.0F};

    friend bool operator==(const EdgeInsets&, const EdgeInsets&) = default;
};

struct FontRequest {
    std::string Path;
    float       SizeLogical{0.0F};

    friend bool operator==(const FontRequest&, const FontRequest&) = default;
};

// CSS's easing keywords. Ease, CSS's default, starts briskly and settles gently.
enum class Easing { Linear, Ease, EaseIn, EaseOut, EaseInOut };

// One entry of a `transition` list: the property it eases, as written ("flex-grow",
// "background-color", "all", ...), over how long, along which curve. Which properties a
// backend can actually ease is the backend's business; Cosmos keeps whatever was named.
struct Transition {
    std::string Property;
    float       DurationSeconds{0.0F};
    Easing      Curve{Easing::Ease};

    friend bool operator==(const Transition&, const Transition&) = default;
};

enum class Display { Stack, Inline };
enum class FlexDirection { Row, Column };
enum class Align { Start, Center, End, Stretch };
// Main-axis distribution, parallel to Align's cross-axis alignment above --
// CSS's justify-content, scoped to the four keyword values a known consumer
// (pharos-proto's hand-rolled `ThreeZoneRow`) actually needs. No `SpaceAround`/
// `SpaceEvenly`: not requested by anything, see docs/next_steps.md.
enum class Justify { Start, Center, End, SpaceBetween };
enum class TextOverflow { Clip, Ellipsis };
enum class WhiteSpace { Nowrap, Normal };
enum class FontStyle { Normal, Italic };

struct TextDecoration {
    bool Underline{false};
    bool LineThrough{false};

    friend bool operator==(const TextDecoration&, const TextDecoration&) = default;
};

enum class LengthUnit { Px, Percent, Vw, Vh };

struct Length {
    float      Value{0.0F};
    LengthUnit Unit{LengthUnit::Px};

    friend bool operator==(const Length&, const Length&) = default;
};

// CSS's position. Static, the initial value, is normal layout, where the offsets do
// nothing. Penumbra does the layout; Cosmos only says which scheme.
enum class Position { Static, Relative, Absolute, Fixed, Sticky };

// CSS's pointer-events. None lets the pointer through to whatever is under the element.
enum class PointerEvents { Auto, None };

// One of top, right, bottom and left: a length, or `auto`, CSS's initial value. Auto is
// kept rather than left unset so a later rule, a pseudo-class overlay or @starting-style
// can set an offset back to it. A percentage is of the containing block's size.
struct Offset {
    bool   Auto{false};
    Length Value; // unused when Auto

    friend bool operator==(const Offset&, const Offset&) = default;
};

// z-index: `auto`, CSS's initial value, or an integer stack level, negative included.
struct StackLevel {
    bool Auto{false};
    int  Level{0}; // unused when Auto

    friend bool operator==(const StackLevel&, const StackLevel&) = default;
};

// A property whose value an `attr()` names: the backend reads the element's own prop
// called Attribute and sets Property from it, as a number in that property's own unit
// (logical pixels for a length). Cosmos can't know the value; it only says where it
// comes from.
struct AttrBinding {
    std::string Property;
    std::string Attribute;

    friend bool operator==(const AttrBinding&, const AttrBinding&) = default;
};

// One channel of a colour attr() binds, from 0 to 255 like the others, alpha included:
// the element's own number prop called Attribute when that is set, else Value.
struct ColorChannel {
    std::optional<std::string> Attribute;
    std::uint8_t               Value{0}; // used when Attribute is unset

    friend bool operator==(const ColorChannel&, const ColorChannel&) = default;
};

// A colour property set from the element's props by an rgb()/rgba() with at least one
// attr() channel: `background-color: rgba(attr(r), attr(g), attr(b), 255)`. Property is
// `background-color`, `background-gradient-start`, `background-gradient-end`,
// `border-color` or `color`. The backend reads each named prop as a number, rounds it and
// clamps it to 0..255; when one of them is missing, the property stays unset.
struct ColorAttrBinding {
    std::string  Property;
    ColorChannel R;
    ColorChannel G;
    ColorChannel B;
    ColorChannel A;

    friend bool operator==(const ColorAttrBinding&, const ColorAttrBinding&) = default;
};

// The chart properties, read only by the chart primitives (<BarChart>, <LineChart>,
// <DonutChart>). Lengths are logical pixels; AreaOpacity and BarGap are fractions;
// SegmentGap is degrees. A series' or slice's own colour is its ordinary `color`.
struct ChartProperties {
    std::optional<Color> AxisColor;        // axis-color: the baseline
    std::optional<float> AxisWidth;        // axis-width
    std::optional<Color> GridColor;        // grid-color: one line per value tick
    std::optional<float> GridWidth;        // grid-width
    std::optional<Color> HoverColor;       // hover-color: behind or over the hovered entry
    std::optional<float> TickSpacing;      // tick-spacing: the least distance between ticks
    std::optional<float> LabelGap;         // label-gap
    std::optional<float> LineWidth;        // line-width: a line chart's lines
    std::optional<float> PointRadius;      // point-radius
    std::optional<float> HoverPointRadius; // hover-point-radius
    std::optional<float> AreaOpacity;      // area-opacity: the fill under a line
    std::optional<float> BarGap;           // bar-gap: the share of a category's band left empty
    std::optional<float> BarRadius;        // bar-radius
    std::optional<float> DonutThickness;   // donut-thickness: 0 is a pie
    std::optional<float> SegmentGap;       // segment-gap: between slices, in degrees
    std::optional<float> AppearSeconds;    // appear-duration: growing in when shown
    std::optional<float> ChangeSeconds;    // change-duration: easing to new data

    friend bool operator==(const ChartProperties&, const ChartProperties&) = default;
};

struct ResolvedStyle {
    std::optional<Color>       BackgroundColor;
    std::optional<Color>       BackgroundGradientStart;
    std::optional<Color>       BackgroundGradientEnd;
    std::optional<Color>       BorderColor;
    std::optional<float>       BorderWidth;
    std::optional<float>       BorderRadius;
    std::optional<EdgeInsets>  Padding;
    std::optional<EdgeInsets>  Margin;
    std::optional<Color>       TextColor;
    std::optional<std::string> FontFamily;
    std::optional<float>       FontSizeLogical;
    std::optional<FontRequest> Font;
    std::optional<Display>       DisplayMode;
    std::optional<FlexDirection> FlexDirectionMode;
    std::optional<float>         Gap;
    std::optional<Align>         AlignItems;
    std::optional<Justify>       JustifyContent;
    std::vector<Transition>        Transitions; // a later `transition` declaration replaces the whole list
    std::optional<Color> ShadowColor;
    std::optional<float> ShadowBlurRadiusLogical;

    std::shared_ptr<ResolvedStyle> Hover;
    std::shared_ptr<ResolvedStyle> Active;
    std::shared_ptr<ResolvedStyle> Disabled;

    // From `@starting-style { }` blocks: the values an element starts from when it
    // appears, overriding its own for the properties set here, so its transitions ease
    // it in. Null when no rule that matched it has one.
    std::shared_ptr<ResolvedStyle> Starting;

    std::optional<Length> Width;
    std::optional<Length> Height;
    std::optional<Length> MinWidth;
    std::optional<Length> MaxWidth;
    std::optional<float>  TransformScale;

    std::optional<TextOverflow> TextOverflowMode;
    std::optional<WhiteSpace>   WhiteSpaceMode;
    std::optional<FontStyle>      FontStyleMode;
    std::optional<TextDecoration> TextDecorationLine;

    std::optional<float> FlexGrow;
    std::optional<float> Opacity; // 0 to 1, the whole element and everything in it

    // Unset means CSS's initial value: static, auto offsets, auto z-index and, once no
    // ancestor sets it either, pointer-events: auto.
    std::optional<Position>      PositionMode;
    std::optional<Offset>        Top;
    std::optional<Offset>        Right;
    std::optional<Offset>        Bottom;
    std::optional<Offset>        Left;
    std::optional<StackLevel>    ZIndex;
    std::optional<PointerEvents> PointerEventsMode; // inherited, as in CSS

    // The properties set by attr(), which have no value here until the backend reads
    // the element's props. A property is either bound here or set above, never both.
    std::vector<AttrBinding>      AttrBindings;
    std::vector<ColorAttrBinding> ColorAttrBindings;

    ChartProperties Chart;
    std::optional<Color> ScrollbarThumbColor;
    std::optional<Color> ScrollbarTrackColor;
    std::optional<float> ScrollbarWidthLogical;
    std::optional<Color> SelectionColor;
};

} // namespace Cosmos
