#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Lustre {

// docs/lustre_core_spec.md §3 — the resolver's output. Backend-agnostic:
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
// backend can actually ease is the backend's business; Lustre keeps whatever was named.
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
    std::optional<Color> ScrollbarThumbColor;
    std::optional<Color> ScrollbarTrackColor;
    std::optional<float> ScrollbarWidthLogical;
};

} // namespace Lustre
