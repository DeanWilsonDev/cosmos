#include "cimmerian/test.hpp"

#include "Lustre/Parser.h"
#include "Lustre/Resolver.h"

using namespace Lustre;

namespace {

// A minimal, in-memory IStyleTarget for testing -- stands in for whatever
// real Iris tree a host adapter would wrap.
class FakeElement : public IStyleTarget {
public:
    FakeElement(std::string ClassNameValue, std::string PrimitiveTagValue, FakeElement* ParentValue = nullptr,
                bool ComponentRoot = false)
        : ClassName_(std::move(ClassNameValue)), PrimitiveTag_(std::move(PrimitiveTagValue)), Parent_(ParentValue),
          ComponentRoot_(ComponentRoot) {}

    std::string          ClassName() const override { return ClassName_; }
    std::string          PrimitiveTag() const override { return PrimitiveTag_; }
    bool                  IsComponentRoot() const override { return ComponentRoot_; }
    const IStyleTarget*   Parent() const override { return Parent_; }

private:
    std::string  ClassName_;
    std::string  PrimitiveTag_;
    FakeElement* Parent_;
    bool         ComponentRoot_;
};

std::optional<Stylesheet> ParseOrFail(const std::string& Source, const std::string& FilePath) {
    Parser TheParser(Source, FilePath);
    ParseResult Result = TheParser.Parse();
    if (!Result.Errors.empty()) {
        return std::nullopt;
    }
    return std::move(Result.Sheet);
}

} // namespace

DESCRIBE("Resolver", {
    IT("resolves the HealthBar worked example end to end", {
        const auto Sheet = ParseOrFail(R"(
.health-bar {
    --bar-background: #333333;

    background-color: var(--bar-background);
    border-radius: 8px;
}

.bar-critical {
    background-color: #E8593C;
}
)",
                                        "HealthBar.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Bar("bar-critical", "Frame", nullptr, /*ComponentRoot=*/true);

        Resolver                          R;
        std::vector<ResolveDiagnostic>    Diagnostics;
        const ResolvedStyle Style = R.Resolve(Bar, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_TRUE(Diagnostics.empty());
        REQUIRE_TRUE(Style.BackgroundColor.has_value());
        ASSERT_TRUE(Style.BackgroundColor->R == 0xE8 && Style.BackgroundColor->G == 0x59 && Style.BackgroundColor->B == 0x3C);
        // .bar-critical's own background-color wins for this element --
        // .health-bar's border-radius doesn't apply, since Bar itself only
        // carries class="bar-critical", not "health-bar".
        ASSERT_FALSE(Style.BorderRadius.has_value());
    });

    IT("resolves a background-gradient-start/-end pair", {
        const auto Sheet = ParseOrFail(R"(
.selected-row {
    background-gradient-start: #4A90FF;
    background-gradient-end: #2A5ADD;
}
)",
                                        "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Row("selected-row", "Frame", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Row, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.BackgroundGradientStart.has_value());
        REQUIRE_TRUE(Style.BackgroundGradientEnd.has_value());
        ASSERT_TRUE(Style.BackgroundGradientStart->R == 0x4A && Style.BackgroundGradientStart->G == 0x90 &&
                    Style.BackgroundGradientStart->B == 0xFF);
        ASSERT_TRUE(Style.BackgroundGradientEnd->R == 0x2A && Style.BackgroundGradientEnd->G == 0x5A &&
                    Style.BackgroundGradientEnd->B == 0xDD);
    });

    IT("a lone background-gradient-start with no -end resolves to neither", {
        const auto Sheet = ParseOrFail(".selected-row { background-gradient-start: #4A90FF; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Row("selected-row", "Frame", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Row, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_FALSE(Style.BackgroundGradientStart.has_value());
        ASSERT_FALSE(Style.BackgroundGradientEnd.has_value());
    });

    IT("a gradient pair split across global and component layers still resolves", {
        const auto GlobalSheet = ParseOrFail(".selected-row { background-gradient-start: #4A90FF; }", "global.lustre");
        const auto ComponentSheet = ParseOrFail(".selected-row { background-gradient-end: #2A5ADD; }", "Card.lustre");
        REQUIRE_TRUE(GlobalSheet.has_value());
        REQUIRE_TRUE(ComponentSheet.has_value());

        FakeElement Row("selected-row", "Frame", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Row, StylesheetSet{&*GlobalSheet, &*ComponentSheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.BackgroundGradientStart.has_value());
        REQUIRE_TRUE(Style.BackgroundGradientEnd.has_value());
    });

    IT("resolves a box-shadow color+blur-radius shorthand", {
        const auto Sheet = ParseOrFail(R"(
.gradient-button {
    box-shadow: #000000AA 12px;
}
)",
                                        "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Button("gradient-button", "Frame", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Button, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.ShadowColor.has_value());
        REQUIRE_TRUE(Style.ShadowBlurRadiusLogical.has_value());
        ASSERT_TRUE(Style.ShadowColor->R == 0x00 && Style.ShadowColor->G == 0x00 && Style.ShadowColor->B == 0x00 &&
                    Style.ShadowColor->A == 0xAA);
        ASSERT_TRUE(*Style.ShadowBlurRadiusLogical == 12.0F);
    });

    IT("a lone box-shadow color with no blur radius resolves to neither", {
        const auto Sheet = ParseOrFail(".gradient-button { box-shadow: #000000AA; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Button("gradient-button", "Frame", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Button, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_FALSE(Style.ShadowColor.has_value());
        ASSERT_FALSE(Style.ShadowBlurRadiusLogical.has_value());
    });

    IT("resolves max-width and text-overflow: ellipsis", {
        const auto Sheet = ParseOrFail(R"(
.inspector-row-value {
    max-width: 220px;
    text-overflow: ellipsis;
}
)",
                                        "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Value("inspector-row-value", "Text", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Value, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.MaxWidth.has_value());
        ASSERT_TRUE(*Style.MaxWidth == (Length{220.0F, LengthUnit::Px}));
        REQUIRE_TRUE(Style.TextOverflowMode.has_value());
        ASSERT_TRUE(*Style.TextOverflowMode == TextOverflow::Ellipsis);
    });

    IT("resolves text-overflow: clip", {
        const auto Sheet = ParseOrFail(".x { max-width: 100px; text-overflow: clip; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Value("x", "Text", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Value, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.TextOverflowMode.has_value());
        ASSERT_TRUE(*Style.TextOverflowMode == TextOverflow::Clip);
    });

    IT("max-width with no text-overflow still resolves (clamps size, no truncation mode set)", {
        const auto Sheet = ParseOrFail(".x { max-width: 100px; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Value("x", "Text", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Value, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.MaxWidth.has_value());
        ASSERT_FALSE(Style.TextOverflowMode.has_value());
    });

    IT("keeps the unit on width, height, min-width and max-width", {
        const auto Sheet = ParseOrFail(
            ".x { width: 600px; height: 50vh; min-width: 200px; max-width: 100%; }\n.y { width: 80vw; }",
            "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement X("x", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Y("y", "Frame", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle XStyle = R.Resolve(X, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);
        const ResolvedStyle YStyle = R.Resolve(Y, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_TRUE(Diagnostics.empty());
        REQUIRE_TRUE(XStyle.Width.has_value());
        ASSERT_TRUE(*XStyle.Width == (Length{600.0F, LengthUnit::Px}));
        REQUIRE_TRUE(XStyle.Height.has_value());
        ASSERT_TRUE(*XStyle.Height == (Length{50.0F, LengthUnit::Vh}));
        REQUIRE_TRUE(XStyle.MinWidth.has_value());
        ASSERT_TRUE(*XStyle.MinWidth == (Length{200.0F, LengthUnit::Px}));
        REQUIRE_TRUE(XStyle.MaxWidth.has_value());
        ASSERT_TRUE(*XStyle.MaxWidth == (Length{100.0F, LengthUnit::Percent}));
        REQUIRE_TRUE(YStyle.Width.has_value());
        ASSERT_TRUE(*YStyle.Width == (Length{80.0F, LengthUnit::Vw}));
    });

    IT("resolves a unitless width, rem and em as px", {
        const auto Sheet = ParseOrFail(".x { width: 40; height: 2rem; max-width: 3em; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Value("x", "Frame", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Value, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_TRUE(*Style.Width == (Length{40.0F, LengthUnit::Px}));
        ASSERT_TRUE(*Style.Height == (Length{2.0F, LengthUnit::Px}));
        ASSERT_TRUE(*Style.MaxWidth == (Length{3.0F, LengthUnit::Px}));
    });

    IT("rejects %, vw and vh on px-only properties with a diagnostic", {
        const auto Sheet =
            ParseOrFail(".x { gap: 10%; padding: 4px 5vw; border-radius: 2vh; font-size: 50%; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Value("x", "Frame", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Value, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_FALSE(Style.Gap.has_value());
        ASSERT_FALSE(Style.Padding.has_value());
        ASSERT_FALSE(Style.BorderRadius.has_value());
        ASSERT_EQUAL(Diagnostics.size(), std::size_t{4});
        ASSERT_TRUE(Diagnostics[0].Message.find("gap: 10%") != std::string::npos);
        ASSERT_TRUE(Diagnostics[1].Message.find("padding: 5vw") != std::string::npos);
    });

    IT("resolves negative margins, directly and through a variable", {
        const auto Sheet = ParseOrFail(":root { --pull: -8px; }\n.x { margin: 0px 2px -4px -6.5px; }\n.y { margin: var(--pull); }",
                                       "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement X("x", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Y("y", "Frame", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle XStyle = R.Resolve(X, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);
        const ResolvedStyle YStyle = R.Resolve(Y, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_TRUE(Diagnostics.empty());
        REQUIRE_TRUE(XStyle.Margin.has_value());
        ASSERT_TRUE(*XStyle.Margin == (EdgeInsets{-6.5F, 0.0F, 2.0F, -4.0F}));
        REQUIRE_TRUE(YStyle.Margin.has_value());
        ASSERT_TRUE(*YStyle.Margin == (EdgeInsets{-8.0F, -8.0F, -8.0F, -8.0F}));
    });

    IT("rejects a negative length anywhere but margin with a diagnostic", {
        const auto Sheet = ParseOrFail(
            ":root { --pull: -8px; }\n.x { padding: 4px -2px; gap: -6px; width: -10%; font-size: var(--pull); }",
            "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Value("x", "Frame", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Value, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_FALSE(Style.Padding.has_value());
        ASSERT_FALSE(Style.Gap.has_value());
        ASSERT_FALSE(Style.Width.has_value());
        ASSERT_FALSE(Style.FontSizeLogical.has_value());
        ASSERT_EQUAL(Diagnostics.size(), std::size_t{4});
        ASSERT_TRUE(Diagnostics[0].Message.find("padding: -2px") != std::string::npos);
        ASSERT_TRUE(Diagnostics[0].Message.find("only `margin`") != std::string::npos);
        ASSERT_TRUE(Diagnostics[3].Message.find("font-size: -8px") != std::string::npos);
    });

    IT("resolves white-space: normal", {
        const auto Sheet = ParseOrFail(".x { white-space: normal; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Value("x", "Text", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Value, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.WhiteSpaceMode.has_value());
        ASSERT_TRUE(*Style.WhiteSpaceMode == WhiteSpace::Normal);
    });

    IT("resolves white-space: nowrap", {
        const auto Sheet = ParseOrFail(".x { white-space: nowrap; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Value("x", "Text", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Value, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.WhiteSpaceMode.has_value());
        ASSERT_TRUE(*Style.WhiteSpaceMode == WhiteSpace::Nowrap);
    });

    IT("resolves flex-grow as a unitless number", {
        const auto Sheet = ParseOrFail(".x { flex-grow: 2; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Value("x", "Scroll", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Value, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.FlexGrow.has_value());
        ASSERT_EQUAL(*Style.FlexGrow, 2.0F);
        ASSERT_TRUE(Diagnostics.empty());
    });

    IT("resolves flex-grow on a leaf without a container-only diagnostic", {
        const auto Sheet = ParseOrFail(".x { flex-grow: 1; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Value("x", "Text", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Value, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.FlexGrow.has_value());
        ASSERT_EQUAL(*Style.FlexGrow, 1.0F);
        ASSERT_TRUE(Diagnostics.empty());
    });

    IT("resolves scrollbar-color with a thumb and a track", {
        const auto Sheet = ParseOrFail(".x { scrollbar-color: #3A3A48 #20202880; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Value("x", "Scroll", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Value, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.ScrollbarThumbColor.has_value());
        ASSERT_TRUE(*Style.ScrollbarThumbColor == (Color{0x3A, 0x3A, 0x48, 0xFF}));
        REQUIRE_TRUE(Style.ScrollbarTrackColor.has_value());
        ASSERT_TRUE(*Style.ScrollbarTrackColor == (Color{0x20, 0x20, 0x28, 0x80}));
    });

    IT("resolves scrollbar-color with only a thumb", {
        const auto Sheet = ParseOrFail(".x { scrollbar-color: #3A3A48; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Value("x", "Scroll", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Value, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.ScrollbarThumbColor.has_value());
        ASSERT_FALSE(Style.ScrollbarTrackColor.has_value());
    });

    IT("resolves a :hover scrollbar-color into the hover overlay", {
        const auto Sheet = ParseOrFail(".x { scrollbar-color: #3A3A48; }\n.x:hover { scrollbar-color: #4C5287; }",
                                       "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Value("x", "Scroll", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Value, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.Hover != nullptr);
        REQUIRE_TRUE(Style.Hover->ScrollbarThumbColor.has_value());
        ASSERT_TRUE(*Style.Hover->ScrollbarThumbColor == (Color{0x4C, 0x52, 0x87, 0xFF}));
    });

    IT("resolves scrollbar-width as a length", {
        const auto Sheet = ParseOrFail(".x { scrollbar-width: 6px; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Value("x", "Scroll", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Value, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.ScrollbarWidthLogical.has_value());
        ASSERT_EQUAL(*Style.ScrollbarWidthLogical, 6.0F);
    });

    IT("matches the textarea primitive selector against a TextArea element", {
        const auto Sheet = ParseOrFail("textarea { background-color: #101014; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Value("", "TextArea", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Value, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.BackgroundColor.has_value());
        ASSERT_TRUE(*Style.BackgroundColor == (Color{0x10, 0x10, 0x14, 0xFF}));
    });

    IT("diagnoses a container-only property on a TextArea", {
        const auto Sheet = ParseOrFail(".x { display: stack; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Value("x", "TextArea", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        R.Resolve(Value, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_EQUAL(Diagnostics.size(), static_cast<std::size_t>(1));
    });

    IT("resolves a descendant selector across real ancestors", {
        const auto Sheet = ParseOrFail(R"(
.card {
    padding: 16px;

    .card-title {
        color: #FFFFFF;
    }
}
)",
                                        "Card.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Card("card", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Title("card-title", "Text", &Card);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Title, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_TRUE(Diagnostics.empty());
        REQUIRE_TRUE(Style.TextColor.has_value());
        ASSERT_TRUE(Style.TextColor->R == 0xFF && Style.TextColor->G == 0xFF && Style.TextColor->B == 0xFF);
    });

    IT("does not let a descendant selector cross a child-component boundary", {
        const auto Sheet = ParseOrFail(R"(
.card {
    .health-bar-fill {
        background-color: #FF0000;
    }
}
)",
                                        "Card.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Card("card", "Frame", nullptr, /*ComponentRoot=*/true);
        // HealthBarRoot stands in for a mounted <HealthBar/> instance's own
        // root -- IsComponentRoot() is true for it because, from Card's
        // resolver call, everything at and below it belongs to a different
        // component's own file (docs/lustre_core_spec.md §1.2).
        FakeElement HealthBarRoot("health-bar", "Frame", &Card, /*ComponentRoot=*/true);
        FakeElement Fill("health-bar-fill", "Frame", &HealthBarRoot);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Fill, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        // `.card .health-bar-fill` never matches -- the ancestor walk from
        // Fill stops at HealthBarRoot (its own component boundary) and
        // never reaches Card.
        ASSERT_FALSE(Style.BackgroundColor.has_value());
    });

    IT("lets global.lustre reach across component boundaries", {
        const auto Sheet = ParseOrFail(R"(
.card {
    .health-bar-fill {
        background-color: #FF0000;
    }
}
)",
                                        "global.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Card("card", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement HealthBarRoot("health-bar", "Frame", &Card, /*ComponentRoot=*/true);
        FakeElement Fill("health-bar-fill", "Frame", &HealthBarRoot);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Fill, StylesheetSet{&*Sheet, nullptr}, /*Unbounded=*/true, Diagnostics);

        REQUIRE_TRUE(Style.BackgroundColor.has_value());
        ASSERT_TRUE(Style.BackgroundColor->R == 0xFF);
    });

    IT("lets a component's own file override global.lustre for the same class", {
        const auto GlobalSheet = ParseOrFail(".card { background-color: #000000; }", "global.lustre");
        const auto ComponentSheet = ParseOrFail(".card { background-color: #FFFFFF; }", "Card.lustre");
        REQUIRE_TRUE(GlobalSheet.has_value());
        REQUIRE_TRUE(ComponentSheet.has_value());

        FakeElement Card("card", "Frame", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style =
            R.Resolve(Card, StylesheetSet{&*GlobalSheet, &*ComponentSheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.BackgroundColor.has_value());
        ASSERT_TRUE(Style.BackgroundColor->R == 0xFF); // component layer wins over global
    });

    IT("resolves a pseudo-class overlay separately from the base style", {
        const auto Sheet = ParseOrFail(R"(
.button {
    background-color: #4CAF50;
}

.button:hover {
    background-color: #66BB6A;
}
)",
                                        "Button.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Button("button", "Frame", nullptr, true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Button, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.BackgroundColor.has_value());
        ASSERT_TRUE(Style.BackgroundColor->G == 0xAF); // base .button

        REQUIRE_TRUE(Style.Hover != nullptr);
        REQUIRE_TRUE(Style.Hover->BackgroundColor.has_value());
        ASSERT_TRUE(Style.Hover->BackgroundColor->G == 0xBB); // :hover overlay, distinct from base
    });

    IT("reports an undefined variable reference", {
        const auto Sheet = ParseOrFail(".card { background-color: var(--not-declared); }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Card("card", "Frame", nullptr, true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Card, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_FALSE(Diagnostics.empty());
        ASSERT_FALSE(Style.BackgroundColor.has_value());
    });

    IT("shadows a global variable with a component-scoped one of the same name", {
        const auto GlobalSheet = ParseOrFail(":root { --accent: #000000; }", "global.lustre");
        const auto ComponentSheet = ParseOrFail(R"(
.card {
    --accent: #FFFFFF;

    background-color: var(--accent);
}
)",
                                                 "Card.lustre");
        REQUIRE_TRUE(GlobalSheet.has_value());
        REQUIRE_TRUE(ComponentSheet.has_value());

        FakeElement Card("card", "Frame", nullptr, true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style =
            R.Resolve(Card, StylesheetSet{&*GlobalSheet, &*ComponentSheet}, false, Diagnostics);

        ASSERT_TRUE(Diagnostics.empty());
        REQUIRE_TRUE(Style.BackgroundColor.has_value());
        ASSERT_TRUE(Style.BackgroundColor->R == 0xFF); // component-scoped --accent shadows global.lustre's
    });

    IT("reports display: stack applied to a leaf primitive instead of silently resolving it", {
        const auto Sheet = ParseOrFail(".json-path { display: stack; }", "Toolbar.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Field("json-path", "Input", nullptr, true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Field, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_FALSE(Diagnostics.empty());
        ASSERT_FALSE(Style.DisplayMode.has_value());
    });

    IT("still resolves display: stack on a real container", {
        const auto Sheet = ParseOrFail(".toolbar { display: stack; }", "Toolbar.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Bar("toolbar", "Frame", nullptr, true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Bar, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_TRUE(Diagnostics.empty());
        REQUIRE_TRUE(Style.DisplayMode.has_value());
        ASSERT_TRUE(*Style.DisplayMode == Display::Stack);
    });

    IT("resolves justify-content: space-between on a container", {
        const auto Sheet = ParseOrFail(".three-zone-row { justify-content: space-between; }", "ThreeZoneRow.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Row("three-zone-row", "Frame", nullptr, true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Row, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_TRUE(Diagnostics.empty());
        REQUIRE_TRUE(Style.JustifyContent.has_value());
        ASSERT_TRUE(*Style.JustifyContent == Justify::SpaceBetween);
    });

    IT("reports justify-content applied to a leaf primitive instead of silently resolving it", {
        const auto Sheet = ParseOrFail(".json-path { justify-content: center; }", "Toolbar.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Field("json-path", "Input", nullptr, true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Field, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_FALSE(Diagnostics.empty());
        ASSERT_FALSE(Style.JustifyContent.has_value());
    });

    IT("resolves font-style: italic and normal", {
        const auto Sheet = ParseOrFail(".slanted { font-style: italic; } .upright { font-style: normal; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Slanted("slanted", "Text", nullptr, /*ComponentRoot=*/true);
        FakeElement Upright("upright", "Text", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle SlantedStyle = R.Resolve(Slanted, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);
        const ResolvedStyle UprightStyle = R.Resolve(Upright, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_TRUE(Diagnostics.empty());
        REQUIRE_TRUE(SlantedStyle.FontStyleMode.has_value());
        ASSERT_TRUE(*SlantedStyle.FontStyleMode == FontStyle::Italic);
        REQUIRE_TRUE(UprightStyle.FontStyleMode.has_value());
        ASSERT_TRUE(*UprightStyle.FontStyleMode == FontStyle::Normal);
    });

    IT("resolves text-decoration lines, alone and together", {
        const auto Sheet = ParseOrFail(R"(
.under { text-decoration: underline; }
.struck { text-decoration: line-through; }
.both { text-decoration: underline line-through; }
.plain { text-decoration: none; }
)",
                                        "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        auto Resolve = [&](const char* ClassName) {
            FakeElement Element(ClassName, "Text", nullptr, /*ComponentRoot=*/true);
            return R.Resolve(Element, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);
        };

        const ResolvedStyle Under = Resolve("under");
        const ResolvedStyle Struck = Resolve("struck");
        const ResolvedStyle Both = Resolve("both");
        const ResolvedStyle Plain = Resolve("plain");

        ASSERT_TRUE(Diagnostics.empty());
        REQUIRE_TRUE(Under.TextDecorationLine.has_value());
        ASSERT_TRUE(*Under.TextDecorationLine == (TextDecoration{.Underline = true, .LineThrough = false}));
        REQUIRE_TRUE(Struck.TextDecorationLine.has_value());
        ASSERT_TRUE(*Struck.TextDecorationLine == (TextDecoration{.Underline = false, .LineThrough = true}));
        REQUIRE_TRUE(Both.TextDecorationLine.has_value());
        ASSERT_TRUE(*Both.TextDecorationLine == (TextDecoration{.Underline = true, .LineThrough = true}));
        REQUIRE_TRUE(Plain.TextDecorationLine.has_value());
        ASSERT_TRUE(*Plain.TextDecorationLine == TextDecoration{});
    });

    IT("ignores an unknown text-decoration keyword", {
        const auto Sheet = ParseOrFail(".x { text-decoration: overline; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Element("x", "Text", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Element, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_FALSE(Style.TextDecorationLine.has_value());
    });

    IT("matches every class in a space-separated class list, later rules winning", {
        const auto Sheet = ParseOrFail(R"(
.body { color: #101010; font-size: 14px; }
.strong { font-family: "bold.ttf"; }
.link { color: #20A0F0; text-decoration: underline; }
)",
                                        "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Element("link  body\tstrong", "Text", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Element, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.TextColor.has_value());
        ASSERT_TRUE(*Style.TextColor == (Color{0x20, 0xA0, 0xF0, 0xFF}));
        REQUIRE_TRUE(Style.Font.has_value());
        ASSERT_TRUE(Style.Font->Path == "bold.ttf");
        ASSERT_TRUE(Style.Font->SizeLogical == 14.0F);
        REQUIRE_TRUE(Style.TextDecorationLine.has_value());
        ASSERT_TRUE(Style.TextDecorationLine->Underline);
    });

    IT("does not match a class that is only a prefix of one in the list", {
        const auto Sheet = ParseOrFail(".body { color: #101010; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Element("body-text other", "Text", nullptr, /*ComponentRoot=*/true);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Element, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_FALSE(Style.TextColor.has_value());
    });

    IT("matches a descendant selector against one class of an ancestor's list", {
        const auto Sheet = ParseOrFail(".card { .label { color: #ABCDEF; } }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Card("panel card", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Label("label wide", "Text", &Card);

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Label, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.TextColor.has_value());
        ASSERT_TRUE(*Style.TextColor == (Color{0xAB, 0xCD, 0xEF, 0xFF}));
    });

    IT("resolves a transition list with easings and seconds or milliseconds", {
        const auto Sheet = ParseOrFail(".bar { transition: flex-grow 0.6s ease-out, background-color 200ms; }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Element("bar", "Frame", nullptr, /*ComponentRoot=*/true);
        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Element, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_TRUE(Diagnostics.empty());
        REQUIRE_EQUAL(Style.Transitions.size(), static_cast<std::size_t>(2));
        ASSERT_TRUE(Style.Transitions[0] == (Transition{"flex-grow", 0.6F, Easing::EaseOut}));
        ASSERT_TRUE(Style.Transitions[1] == (Transition{"background-color", 0.2F, Easing::Ease}));
    });

    IT("lets a later transition declaration replace the whole list", {
        const auto Sheet = ParseOrFail(R"(
.bar { transition: width 1s, height 1s; }
.bar-fast { transition: width 0.1s linear; }
)", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Element("bar bar-fast", "Frame", nullptr, /*ComponentRoot=*/true);
        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Element, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_EQUAL(Style.Transitions.size(), static_cast<std::size_t>(1));
        ASSERT_TRUE(Style.Transitions[0] == (Transition{"width", 0.1F, Easing::Linear}));
    });

    IT("keeps a transition entry's place when its duration comes from a variable", {
        const auto Sheet = ParseOrFail(R"(
.bar {
    --quick: 0.2s;
    transition: width var(--quick), height 1s;
}
)", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Element("bar", "Frame", nullptr, /*ComponentRoot=*/true);
        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Element, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_EQUAL(Style.Transitions.size(), static_cast<std::size_t>(2));
        ASSERT_TRUE(Style.Transitions[0] == (Transition{"width", 0.2F, Easing::Ease}));
        ASSERT_TRUE(Style.Transitions[1] == (Transition{"height", 1.0F, Easing::Ease}));
    });

    IT("resolves opacity from a number or a percentage, and diagnoses a length", {
        const auto Sheet = ParseOrFail(R"(
.half { opacity: 0.5; }
.faint { opacity: 20%; }
.wrong { opacity: 4px; }
)", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        FakeElement Half("half", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Faint("faint", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Wrong("wrong", "Frame", nullptr, /*ComponentRoot=*/true);

        ASSERT_TRUE(R.Resolve(Half, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics).Opacity == 0.5F);
        ASSERT_TRUE(R.Resolve(Faint, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics).Opacity == 0.2F);
        ASSERT_TRUE(Diagnostics.empty());
        ASSERT_FALSE(R.Resolve(Wrong, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics).Opacity.has_value());
        ASSERT_EQUAL(Diagnostics.size(), std::size_t{1});
    });

    IT("resolves @starting-style into the Starting overlay, leaving the element's own values alone", {
        const auto Sheet = ParseOrFail(R"(
.bar {
    flex-grow: 60;
    background-color: #00FF00;
    transition: flex-grow 0.6s ease-out;
    @starting-style { flex-grow: 0; opacity: 0; }
}
)", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Element("bar", "Frame", nullptr, /*ComponentRoot=*/true);
        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Element, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_TRUE(Diagnostics.empty());
        ASSERT_TRUE(Style.FlexGrow == 60.0F);
        ASSERT_FALSE(Style.Opacity.has_value());
        REQUIRE_TRUE(Style.Starting != nullptr);
        ASSERT_TRUE(Style.Starting->FlexGrow == 0.0F);
        ASSERT_TRUE(Style.Starting->Opacity == 0.0F);
        ASSERT_FALSE(Style.Starting->BackgroundColor.has_value());
    });

    IT("diagnoses @starting-style inside a pseudo-class rule and ignores it", {
        const auto Sheet = ParseOrFail(".bar:hover { @starting-style { opacity: 0; } }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Element("bar", "Frame", nullptr, /*ComponentRoot=*/true);
        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Element, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_TRUE(Style.Starting == nullptr);
        REQUIRE_EQUAL(Diagnostics.size(), std::size_t{1});
        ASSERT_TRUE(Diagnostics[0].Message.find("`:hover`") != std::string::npos);
    });

    IT("resolves attr() into a binding, leaving the property itself unset", {
        const auto Sheet = ParseOrFail(".bar { flex-grow: attr(fill); width: attr(size); }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Element("bar", "Frame", nullptr, /*ComponentRoot=*/true);
        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Element, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_TRUE(Diagnostics.empty());
        ASSERT_FALSE(Style.FlexGrow.has_value());
        ASSERT_FALSE(Style.Width.has_value());
        REQUIRE_EQUAL(Style.AttrBindings.size(), static_cast<std::size_t>(2));
        ASSERT_TRUE(Style.AttrBindings[0] == (AttrBinding{"flex-grow", "fill"}));
        ASSERT_TRUE(Style.AttrBindings[1] == (AttrBinding{"width", "size"}));
    });

    IT("lets a later value replace an attr() binding, and a later attr() replace a value", {
        const auto Sheet = ParseOrFail(R"(
.bar { flex-grow: attr(fill); opacity: 0.5; }
.bar-fixed { flex-grow: 3; opacity: attr(alpha); }
)", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Element("bar bar-fixed", "Frame", nullptr, /*ComponentRoot=*/true);
        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Element, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_TRUE(Style.FlexGrow == 3.0F);
        ASSERT_FALSE(Style.Opacity.has_value());
        REQUIRE_EQUAL(Style.AttrBindings.size(), static_cast<std::size_t>(1));
        ASSERT_TRUE(Style.AttrBindings[0] == (AttrBinding{"opacity", "alpha"}));
    });

    IT("diagnoses attr() on a property that doesn't take a single number", {
        const auto Sheet = ParseOrFail(".bar { background-color: attr(tint); }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Element("bar", "Frame", nullptr, /*ComponentRoot=*/true);
        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Element, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_TRUE(Style.AttrBindings.empty());
        REQUIRE_EQUAL(Diagnostics.size(), std::size_t{1});
        ASSERT_TRUE(Diagnostics[0].Message.find("background-color: attr(tint)") != std::string::npos);
    });

    IT("binds attr() inside @starting-style onto the Starting overlay", {
        const auto Sheet =
            ParseOrFail(".bar { flex-grow: attr(fill); @starting-style { flex-grow: attr(from); } }", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Element("bar", "Frame", nullptr, /*ComponentRoot=*/true);
        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Element, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        REQUIRE_TRUE(Style.Starting != nullptr);
        REQUIRE_EQUAL(Style.Starting->AttrBindings.size(), static_cast<std::size_t>(1));
        ASSERT_TRUE(Style.Starting->AttrBindings[0] == (AttrBinding{"flex-grow", "from"}));
    });

    IT("resolves the chart properties into ResolvedStyle::Chart", {
        const auto Sheet = ParseOrFail(R"(
.weekly {
    color: #888888;
    axis-color: #999999;
    axis-width: 1px;
    grid-color: #333333;
    grid-width: 1px;
    hover-color: #FFFFFF22;
    tick-spacing: 28px;
    label-gap: 6px;
    line-width: 2px;
    point-radius: 2.5px;
    hover-point-radius: 4px;
    area-opacity: 25%;
    bar-gap: 0.3;
    bar-radius: 2px;
    donut-thickness: 18px;
    segment-gap: 3deg;
    appear-duration: 0.6s;
    change-duration: 400ms;
}
)", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Element("weekly", "BarChart", nullptr, /*ComponentRoot=*/true);
        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = R.Resolve(Element, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);

        ASSERT_TRUE(Diagnostics.empty());
        const ChartProperties& Chart = Style.Chart;
        ASSERT_TRUE(Chart.AxisColor == (Color{0x99, 0x99, 0x99, 0xFF}));
        ASSERT_TRUE(Chart.HoverColor == (Color{0xFF, 0xFF, 0xFF, 0x22}));
        ASSERT_TRUE(Chart.AxisWidth == 1.0F && Chart.GridWidth == 1.0F && Chart.TickSpacing == 28.0F);
        ASSERT_TRUE(Chart.LabelGap == 6.0F && Chart.LineWidth == 2.0F && Chart.PointRadius == 2.5F);
        ASSERT_TRUE(Chart.HoverPointRadius == 4.0F && Chart.BarRadius == 2.0F && Chart.DonutThickness == 18.0F);
        ASSERT_TRUE(Chart.AreaOpacity == 0.25F && Chart.BarGap == 0.3F && Chart.SegmentGap == 3.0F);
        ASSERT_TRUE(Chart.AppearSeconds == 0.6F && Chart.ChangeSeconds == 0.4F);
    });

    IT("matches chart primitive selectors and diagnoses a bad fraction or angle", {
        const auto Sheet = ParseOrFail(R"(
series { color: #6193FF; }
.weekly { bar-gap: 4px; segment-gap: 2px; }
)", "test.lustre");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Chart("weekly", "BarChart", nullptr, /*ComponentRoot=*/true);
        FakeElement Series("", "Series", &Chart);
        Resolver                       R;
        std::vector<ResolveDiagnostic> Diagnostics;
        ASSERT_TRUE(R.Resolve(Series, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics).TextColor ==
                    (Color{0x61, 0x93, 0xFF, 0xFF}));

        const ResolvedStyle Style = R.Resolve(Chart, StylesheetSet{nullptr, &*Sheet}, false, Diagnostics);
        ASSERT_FALSE(Style.Chart.BarGap.has_value());
        ASSERT_FALSE(Style.Chart.SegmentGap.has_value());
        ASSERT_EQUAL(Diagnostics.size(), std::size_t{2});
    });
});
