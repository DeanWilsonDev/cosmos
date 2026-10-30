#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Cosmos {

// What a property's value looks like once any var() is replaced, which decides what the
// checker can say about it beyond what the resolver reports.
enum class PropertyValueShape {
    Color,         // one colour
    OneOrTwoColors, // scrollbar-color: thumb, then an optional track
    Length,        // one length in px
    EdgeLengths,   // one to four lengths in px, CSS's top, right, bottom, left
    Size,          // one length in px, %, vw or vh
    Number,        // one unitless number
    Keyword,       // exactly one of Keywords
    KeywordList,   // one or more of Keywords
    FontPath,      // one quoted path
    Time,          // one duration, s or ms
    Fraction,      // 0 to 1, or a percentage
    Angle,         // degrees
    Offset,        // auto or a length
    Offsets,       // one to four of auto or a length
    StackLevel,    // auto or an integer
    Scale,         // scale(<number>)
    Transition,    // <property> <time> [<easing>], comma-separated
    Shadow,        // a colour and a blur radius
};

struct PropertyInfo {
    std::string_view               Name;
    PropertyValueShape             Shape;
    std::span<const std::string_view> Keywords;
    bool                           ContainerOnly;
    bool                           Inherited;
    std::string_view               Summary;
};

struct PseudoClassInfo {
    std::string_view Name;
    std::string_view Summary;
};

std::span<const PropertyInfo> AllProperties();
const PropertyInfo*           FindProperty(std::string_view Name);

std::span<const PseudoClassInfo> AllPseudoClasses();

// What the property takes, to follow "`name` takes ", such as "a colour: a hex literal like
// `#4a90ff`, or `rgb()` or `rgba()`".
std::string DescribeValues(const PropertyInfo& Property);

// The candidate closest to Name by edit distance, when it is close enough to be a likely
// typo; empty otherwise.
std::string_view ClosestMatch(std::string_view Name, std::span<const std::string_view> Candidates);

std::vector<std::string_view> PropertyNames();

} // namespace Cosmos
