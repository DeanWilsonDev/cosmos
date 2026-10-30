#include "cimmerian/test.hpp"

#include "Cosmos/Parser.h"
#include "Cosmos/Properties.h"
#include "Cosmos/Resolver.h"

#include <string>
#include <vector>

using namespace Cosmos;

namespace {

class Element : public IStyleTarget {
public:
    explicit Element(std::string Tag) : Tag_(std::move(Tag)) {}

    std::string         ClassName() const override { return "probe"; }
    std::string         PrimitiveTag() const override { return Tag_; }
    bool                IsComponentRoot() const override { return true; }
    const IStyleTarget* Parent() const override { return nullptr; }

private:
    std::string Tag_;
};

struct Resolved {
    ResolvedStyle                  Style;
    std::vector<ResolveDiagnostic> Diagnostics;
};

// A gradient stop applies only with its partner (§2), so each is probed with the other.
std::string Companion(const std::string& Property) {
    if (Property == "background-gradient-start") {
        return " background-gradient-end: #000000;";
    }
    if (Property == "background-gradient-end") {
        return " background-gradient-start: #000000;";
    }
    return "";
}

Resolved ResolveOne(const std::string& Property, const std::string& Value) {
    const std::string Source = ".probe { " + Property + ": " + Value + ";" + Companion(Property) + " }";
    Parser            P(Source, "Probe.cosmos");
    ParseResult Parsed = P.Parse();
    Resolved    Result;
    Result.Style = Resolver{}.Resolve(Element("Frame"), StylesheetSet{nullptr, &*Parsed.Sheet}, false,
                                      Result.Diagnostics);
    return Result;
}

std::string SampleValue(const PropertyInfo& Property) {
    using Shape = PropertyValueShape;
    switch (Property.Shape) {
    case Shape::Color:
    case Shape::OneOrTwoColors: return "#112233";
    case Shape::Length:
    case Shape::EdgeLengths:
    case Shape::Size:
    case Shape::Offset:
    case Shape::Offsets: return "4px";
    case Shape::Number: return "1";
    case Shape::Keyword:
    case Shape::KeywordList: return std::string(Property.Keywords.back());
    case Shape::FontPath: return "\"assets/body.ttf\"";
    case Shape::Time: return "0.2s";
    case Shape::Fraction: return "0.5";
    case Shape::Angle: return "3deg";
    case Shape::StackLevel: return "2";
    case Shape::Scale: return "scale(1.5)";
    case Shape::Transition: return "color 0.2s";
    case Shape::Shadow: return "#112233 4px";
    }
    return "";
}

bool TakesEachKeyword(PropertyValueShape Shape) {
    return Shape == PropertyValueShape::Keyword || Shape == PropertyValueShape::KeywordList ||
           Shape == PropertyValueShape::Offset || Shape == PropertyValueShape::Offsets ||
           Shape == PropertyValueShape::StackLevel;
}

} // namespace

DESCRIBE("Properties", {
    IT("lists each property once", {
        const std::vector<std::string_view> Names = PropertyNames();
        for (std::size_t I = 0; I < Names.size(); ++I) {
            for (std::size_t J = I + 1; J < Names.size(); ++J) {
                ASSERT_NOT_EQUAL(Names[I], Names[J]);
            }
        }
        ASSERT_EQUAL(Names.size(), static_cast<std::size_t>(58));
    });

    IT("has a resolver case for every property in the table", {
        std::vector<std::string> Unapplied;
        for (const PropertyInfo& Property : AllProperties()) {
            const Resolved Result = ResolveOne(std::string(Property.Name), SampleValue(Property));
            if (!Result.Diagnostics.empty() || Result.Style == ResolvedStyle{}) {
                Unapplied.push_back(std::string(Property.Name) + ": " + SampleValue(Property));
            }
        }
        ASSERT_EQUAL(Unapplied, std::vector<std::string>{});
    });

    IT("resolves every keyword the table lists for a property", {
        std::vector<std::string> Unapplied;
        for (const PropertyInfo& Property : AllProperties()) {
            if (!TakesEachKeyword(Property.Shape)) {
                continue;
            }
            for (const std::string_view Keyword : Property.Keywords) {
                const Resolved Result = ResolveOne(std::string(Property.Name), std::string(Keyword));
                if (!Result.Diagnostics.empty() || Result.Style == ResolvedStyle{}) {
                    Unapplied.push_back(std::string(Property.Name) + ": " + std::string(Keyword));
                }
            }
        }
        ASSERT_EQUAL(Unapplied, std::vector<std::string>{});
    });

    IT("ignores a property outside the table without a diagnostic", {
        const Resolved Result = ResolveOne("colr", "#112233");
        ASSERT_TRUE(Result.Diagnostics.empty());
        ASSERT_TRUE(Result.Style == ResolvedStyle{});
    });

    IT("reports a keyword outside a property's set, with the closest one", {
        const Resolved Result = ResolveOne("align-items", "centre");
        REQUIRE_EQUAL(Result.Diagnostics.size(), static_cast<std::size_t>(1));
        ASSERT_EQUAL(Result.Diagnostics[0].Message,
                     std::string("`align-items: centre` -- `align-items` takes `start`, `center`, `end` or "
                                 "`stretch`. Did you mean `center`?"));
        ASSERT_EQUAL(Result.Diagnostics[0].Suggestion, std::string("center"));
        ASSERT_FALSE(Result.Style.AlignItems.has_value());
    });

    IT("reports a value of the wrong shape and keeps the earlier value", {
        Parser      P(".probe { color: #ff0000; color: 12px; }", "Probe.cosmos");
        ParseResult Parsed = P.Parse();
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style =
            Resolver{}.Resolve(Element("Text"), StylesheetSet{nullptr, &*Parsed.Sheet}, false, Diagnostics);
        REQUIRE_EQUAL(Diagnostics.size(), static_cast<std::size_t>(1));
        ASSERT_TRUE(Diagnostics[0].Message.find("`color` takes a colour") != std::string::npos);
        ASSERT_EQUAL(Diagnostics[0].Location.Line, static_cast<std::uint32_t>(1));
        ASSERT_EQUAL(Diagnostics[0].Location.Column, static_cast<std::uint32_t>(26));
        ASSERT_EQUAL(Diagnostics[0].End.Column, static_cast<std::uint32_t>(37));
        ASSERT_EQUAL(Diagnostics[0].Location.FilePath, std::string("Probe.cosmos"));
        REQUIRE_TRUE(Style.TextColor.has_value());
        ASSERT_EQUAL(Style.TextColor->R, static_cast<std::uint8_t>(255));
    });

    IT("marks the container-only and inherited properties", {
        for (const std::string_view Name : {"display", "flex-direction", "gap", "align-items", "justify-content"}) {
            ASSERT_TRUE(FindProperty(Name)->ContainerOnly);
        }
        for (const std::string_view Name : {"color", "font-family", "font-size", "font-style", "pointer-events"}) {
            ASSERT_TRUE(FindProperty(Name)->Inherited);
        }
        ASSERT_FALSE(FindProperty("width")->ContainerOnly);
        ASSERT_FALSE(FindProperty("text-decoration")->Inherited);
    });

    IT("lists the pseudo-classes the parser takes", {
        std::vector<std::string_view> Names;
        for (const PseudoClassInfo& PseudoClass : AllPseudoClasses()) {
            Names.push_back(PseudoClass.Name);
            const std::string Source = ".card:" + std::string(PseudoClass.Name) + " { color: #fff; }";
            Parser            P(Source, "");
            ASSERT_TRUE(P.Parse().Errors.empty());
        }
        ASSERT_EQUAL(Names, (std::vector<std::string_view>{"hover", "active", "disabled"}));
    });

    IT("finds the closest name for a likely typo, and nothing for a distant one", {
        const std::vector<std::string_view> Names = PropertyNames();
        ASSERT_EQUAL(ClosestMatch("colr", Names), std::string_view("color"));
        ASSERT_EQUAL(ClosestMatch("backgound-color", Names), std::string_view("background-color"));
        ASSERT_EQUAL(ClosestMatch("justify-contents", Names), std::string_view("justify-content"));
        ASSERT_TRUE(ClosestMatch("zzz", Names).empty());
        ASSERT_TRUE(ClosestMatch("color", Names).empty());
    });

    IT("describes what a property takes", {
        ASSERT_EQUAL(DescribeValues(*FindProperty("display")), std::string("`stack` or `inline`"));
        ASSERT_EQUAL(DescribeValues(*FindProperty("text-decoration")),
                     std::string("`none`, or any of `underline` or `line-through`"));
        ASSERT_EQUAL(DescribeValues(*FindProperty("width")), std::string("a length in `px`, `%`, `vw` or `vh`"));
    });
});
