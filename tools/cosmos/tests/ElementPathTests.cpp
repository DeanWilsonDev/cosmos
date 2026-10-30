#include "cimmerian/test.hpp"

#include "ElementPath.h"

using namespace CosmosCli;

namespace {

std::vector<std::string> Described(std::string_view Text) {
    const auto Parsed = ParseElementPath(Text);
    std::vector<std::string> Out;
    if (const auto* Error = std::get_if<ElementPathError>(&Parsed)) {
        Out.push_back("error at " + std::to_string(Error->Column) + ": " + Error->Message);
        return Out;
    }
    for (const ElementDescription& Element : std::get<std::vector<ElementDescription>>(Parsed)) {
        Out.push_back(DescribeElement(Element));
    }
    return Out;
}

} // namespace

DESCRIBE("Element paths", {
    IT("reads tags, classes, props and a pseudo-class, outermost first", {
        ASSERT_EQUAL(Described("Frame[class=\"card wide\"] > text[class='title' size=12]:hover"),
                     (std::vector<std::string>{"Frame[class=\"card wide\"]",
                                               "Text[class=\"title\" size=\"12\"]:hover"}));
        ASSERT_EQUAL(Described("  TextArea  "), std::vector<std::string>{"TextArea"});
    });

    IT("reports where a path goes wrong", {
        ASSERT_EQUAL(Described("Frame > Card"),
                     std::vector<std::string>{"error at 9: `Card` isn't an element Cosmos styles: it styles Frame, "
                                              "Inline, Grid, Scroll, Image, Text, Input, TextArea, BarChart, "
                                              "LineChart, DonutChart, Series or Slice"});
        ASSERT_EQUAL(Described("Frame:hover > Text"),
                     std::vector<std::string>{"error at 13: only the last element can take a pseudo-class"});
        ASSERT_EQUAL(Described("Frame[class=\"card]"), std::vector<std::string>{"error at 13: this quote isn't closed"});
        ASSERT_EQUAL(Described("Frame:focus"),
                     std::vector<std::string>{"error at 7: expected `hover`, `active` or `disabled` after `:`"});
        ASSERT_EQUAL(Described("Frame Text"), std::vector<std::string>{"error at 7: expected `>` between elements"});
    });
});
