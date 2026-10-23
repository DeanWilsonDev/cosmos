#include "cimmerian/test.hpp"

#include "Cosmos/Parser.h"
#include "Cosmos/Resolver.h"

using namespace Cosmos;

namespace {

// Same minimal in-memory IStyleTarget ResolverTests.cpp uses.
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

DESCRIBE("Inheritance (§1.7)", {
    IT("a child with no color of its own inherits its parent's", {
        const auto Sheet = ParseOrFail(".row { color: #FFFFFF; }", "test.cosmos");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Row("row", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Label("", "Text", &Row); // no class at all -- nothing of its own could match

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Label, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        REQUIRE_TRUE(Style.TextColor.has_value());
        ASSERT_TRUE(Style.TextColor->R == 0xFF && Style.TextColor->G == 0xFF && Style.TextColor->B == 0xFF);
    });

    IT("a child's own explicit color wins over an inherited one", {
        const auto Sheet = ParseOrFail(R"(
.row { color: #FFFFFF; }
.label { color: #000000; }
)",
                                        "test.cosmos");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Row("row", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Label("label", "Text", &Row);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Label, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        REQUIRE_TRUE(Style.TextColor.has_value());
        ASSERT_TRUE(Style.TextColor->R == 0x00 && Style.TextColor->G == 0x00 && Style.TextColor->B == 0x00);
    });

    IT("inherits through an intermediate node that sets no color of its own", {
        const auto Sheet = ParseOrFail(".ancestor { color: #FFFFFF; }", "test.cosmos");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Grandparent("ancestor", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Parent("", "Frame", &Grandparent); // no class, no rule of its own
        FakeElement Child("", "Text", &Parent);        // same

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Child, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        REQUIRE_TRUE(Style.TextColor.has_value());
        ASSERT_TRUE(Style.TextColor->R == 0xFF);
    });

    IT("the nearest ancestor that sets color wins over a farther one that also sets it", {
        const auto Sheet = ParseOrFail(R"(
.far { color: #FF0000; }
.near { color: #0000FF; }
)",
                                        "test.cosmos");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Grandparent("far", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Parent("near", "Frame", &Grandparent);
        FakeElement Child("", "Text", &Parent);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Child, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        REQUIRE_TRUE(Style.TextColor.has_value());
        ASSERT_TRUE(Style.TextColor->B == 0xFF && Style.TextColor->R == 0x00); // .near's blue, not .far's red
    });

    IT("an element that binds color through attr() neither inherits one nor passes one down", {
        const auto Sheet = ParseOrFail(R"(
.row { color: #FFFFFF; }
.tile { color: rgba(attr(r), attr(g), attr(b), 255); }
)",
                                        "test.cosmos");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Row("row", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Tile("tile", "Frame", &Row);
        FakeElement Label("", "Text", &Tile);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle TileStyle  = ResolveStyle(Tile, StylesheetSet{nullptr, &*Sheet}, Diagnostics);
        const ResolvedStyle LabelStyle = ResolveStyle(Label, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        ASSERT_TRUE(Diagnostics.empty());
        ASSERT_FALSE(TileStyle.TextColor.has_value());
        REQUIRE_EQUAL(TileStyle.ColorAttrBindings.size(), std::size_t{1});
        ASSERT_TRUE(TileStyle.ColorAttrBindings[0].Property == "color");
        ASSERT_FALSE(LabelStyle.TextColor.has_value()); // not .row's white from past the bound tile
        ASSERT_TRUE(LabelStyle.ColorAttrBindings.empty());
    });

    IT("non-inherited properties do not flow down from an ancestor", {
        const auto Sheet = ParseOrFail(".row { background-color: #FF0000; padding: 8px; }", "test.cosmos");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Row("row", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Child("", "Frame", &Row);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Child, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        ASSERT_FALSE(Style.BackgroundColor.has_value());
        ASSERT_FALSE(Style.Padding.has_value());
    });

    IT("font inherits the same way as color", {
        const auto Sheet = ParseOrFail(R"(
.row {
    font-family: "assets/fonts/body.ttf";
    font-size: 14px;
}
)",
                                        "test.cosmos");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Row("row", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Child("", "Text", &Row);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Child, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        REQUIRE_TRUE(Style.Font.has_value());
        ASSERT_TRUE(Style.Font->Path == "assets/fonts/body.ttf");
        ASSERT_TRUE(Style.Font->SizeLogical == 14.0F);
    });

    IT("a font-size of its own keeps the family inherited from an ancestor", {
        const auto Sheet = ParseOrFail(R"(
.row {
    font-family: "assets/fonts/body.ttf";
    font-size: 14px;
}
.title { font-size: 20px; }
)",
                                        "test.cosmos");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Row("row", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Title("title", "Text", &Row);
        FakeElement Inner("", "Text", &Title);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle TitleStyle = ResolveStyle(Title, StylesheetSet{nullptr, &*Sheet}, Diagnostics);
        const ResolvedStyle InnerStyle = ResolveStyle(Inner, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        REQUIRE_TRUE(TitleStyle.Font.has_value());
        ASSERT_TRUE(TitleStyle.Font->Path == "assets/fonts/body.ttf");
        ASSERT_TRUE(TitleStyle.Font->SizeLogical == 20.0F);
        REQUIRE_TRUE(InnerStyle.Font.has_value());
        ASSERT_TRUE(InnerStyle.Font->SizeLogical == 20.0F);
    });

    IT("a font-family of its own keeps the size inherited from an ancestor", {
        const auto Sheet = ParseOrFail(R"(
.block { font-size: 13px; }
.code { font-family: "assets/fonts/mono.ttf"; }
)",
                                        "test.cosmos");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Block("block", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Code("code", "Text", &Block);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Code, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        REQUIRE_TRUE(Style.Font.has_value());
        ASSERT_TRUE(Style.Font->Path == "assets/fonts/mono.ttf");
        ASSERT_TRUE(Style.Font->SizeLogical == 13.0F);
    });

    IT("a font-size with no font-family anywhere gives no font but keeps the size", {
        const auto Sheet = ParseOrFail(".title { font-size: 20px; }", "test.cosmos");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Row("", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Title("title", "Text", &Row);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Title, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        ASSERT_FALSE(Style.Font.has_value());
        ASSERT_FALSE(Style.FontFamily.has_value());
        REQUIRE_TRUE(Style.FontSizeLogical.has_value());
        ASSERT_TRUE(*Style.FontSizeLogical == 20.0F);
    });

    IT("a font-family and a font-size from different rules on one element combine", {
        const auto Sheet = ParseOrFail(R"(
text { font-family: "assets/fonts/body.ttf"; }
.badge { font-size: 11px; }
)",
                                        "test.cosmos");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Badge("badge", "Text", nullptr, /*ComponentRoot=*/true);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Badge, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        REQUIRE_TRUE(Style.Font.has_value());
        ASSERT_TRUE(Style.Font->Path == "assets/fonts/body.ttf");
        ASSERT_TRUE(Style.Font->SizeLogical == 11.0F);
    });

    IT("a root element with no ancestor and no match stays unset, no crash", {
        const auto Sheet = ParseOrFail(".unrelated { color: #FFFFFF; }", "test.cosmos");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Root("", "Frame", nullptr, /*ComponentRoot=*/true); // no parent at all

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Root, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        ASSERT_FALSE(Style.TextColor.has_value());
    });

    IT("a :hover overlay is unaffected by inheritance, even when the base color was inherited", {
        const auto Sheet = ParseOrFail(R"(
.row { color: #FFFFFF; }

.child:hover {
    background-color: #FF0000;
}
)",
                                        "test.cosmos");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Row("row", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Child("child", "Frame", &Row); // has its own class (for :hover to match), but no color rule

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Child, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        REQUIRE_TRUE(Style.TextColor.has_value()); // base value inherited from .row
        ASSERT_TRUE(Style.TextColor->R == 0xFF && Style.TextColor->G == 0xFF && Style.TextColor->B == 0xFF);

        REQUIRE_TRUE(Style.Hover != nullptr);
        ASSERT_TRUE(Style.Hover->BackgroundColor.has_value()); // the child's own :hover rule still applies
        ASSERT_FALSE(Style.Hover->TextColor.has_value());      // but the overlay itself never inherits
    });

    IT("still resolves the two-layer cascade correctly through the new entry point", {
        const auto GlobalSheet = ParseOrFail(".card { color: #000000; }", "global.cosmos");
        const auto ComponentSheet = ParseOrFail(".card { color: #FFFFFF; }", "Card.cosmos");
        REQUIRE_TRUE(GlobalSheet.has_value());
        REQUIRE_TRUE(ComponentSheet.has_value());

        FakeElement Card("card", "Frame", nullptr, /*ComponentRoot=*/true);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Card, StylesheetSet{&*GlobalSheet, &*ComponentSheet}, Diagnostics);

        REQUIRE_TRUE(Style.TextColor.has_value());
        ASSERT_TRUE(Style.TextColor->R == 0xFF); // component layer wins over global, same as Resolver::Resolve()
    });

    IT("a component-layer declaration can reference a variable declared only in the global layer", {
        // Regression test for a real bug (docs/next_steps.md, 2026-08-03): ResolveStyle's
        // two-layer composition used to call Resolver::Resolve() once per layer, each time
        // with the *other* sheet nulled out -- so the component layer's own var() lookups
        // never saw global.cosmos's variables at all, silently dropping any declaration
        // that referenced one (exactly the pattern every real global.cosmos/Component.cosmos
        // pair uses in practice). The test above never caught this because neither layer
        // there uses var() at all -- this one specifically has the *component* layer
        // reference a variable declared only in the *global* layer.
        const auto GlobalSheet = ParseOrFail(":root { --accent: #ABCDEF; }", "global.cosmos");
        const auto ComponentSheet = ParseOrFail(".card { color: var(--accent); }", "Card.cosmos");
        REQUIRE_TRUE(GlobalSheet.has_value());
        REQUIRE_TRUE(ComponentSheet.has_value());

        FakeElement Card("card", "Frame", nullptr, /*ComponentRoot=*/true);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Card, StylesheetSet{&*GlobalSheet, &*ComponentSheet}, Diagnostics);

        ASSERT_TRUE(Diagnostics.empty());
        REQUIRE_TRUE(Style.TextColor.has_value());
        ASSERT_TRUE(Style.TextColor->R == 0xAB && Style.TextColor->G == 0xCD && Style.TextColor->B == 0xEF);
    });

    IT("global.cosmos can still be the source of an inherited value across a component boundary", {
        // Mirrors ResolverTests.cpp's "lets global.cosmos reach across component
        // boundaries", but for inheritance rather than selector matching: an
        // inherited *value* crossing a component root is expected (§1.7), unlike a
        // descendant *selector* crossing one (§1.2), which stays blocked.
        const auto Sheet = ParseOrFail(".app-root { color: #ABCDEF; }", "global.cosmos");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement AppRoot("app-root", "Frame", nullptr, /*ComponentRoot=*/true);
        // ComponentRoot for CardRoot too -- from Label's own resolve, CardRoot is a
        // different component's root, exactly like HealthBarRoot in ResolverTests.cpp.
        FakeElement CardRoot("card", "Frame", &AppRoot, /*ComponentRoot=*/true);
        FakeElement Label("", "Text", &CardRoot);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Label, StylesheetSet{&*Sheet, nullptr}, Diagnostics);

        REQUIRE_TRUE(Style.TextColor.has_value());
        ASSERT_TRUE(Style.TextColor->R == 0xAB && Style.TextColor->G == 0xCD && Style.TextColor->B == 0xEF);
    });

    IT("font-style inherits from an ancestor, and a child's own wins", {
        const auto Sheet = ParseOrFail(".quote { font-style: italic; } .upright { font-style: normal; }", "test.cosmos");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Quote("quote", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Inherits("", "Text", &Quote);
        FakeElement Upright("upright", "Text", &Quote);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Inherited = ResolveStyle(Inherits, StylesheetSet{nullptr, &*Sheet}, Diagnostics);
        const ResolvedStyle Own = ResolveStyle(Upright, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        REQUIRE_TRUE(Inherited.FontStyleMode.has_value());
        ASSERT_TRUE(*Inherited.FontStyleMode == FontStyle::Italic);
        REQUIRE_TRUE(Own.FontStyleMode.has_value());
        ASSERT_TRUE(*Own.FontStyleMode == FontStyle::Normal);
    });

    IT("text-decoration does not inherit", {
        const auto Sheet = ParseOrFail(".link { text-decoration: underline; }", "test.cosmos");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Link("link", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Child("", "Text", &Link);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Child, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        ASSERT_FALSE(Style.TextDecorationLine.has_value());
    });

    IT("pointer-events inherits through any depth, and a descendant's auto overrides an ancestor's none", {
        const auto Sheet = ParseOrFail(".ghost { pointer-events: none; } .live { pointer-events: auto; }", "test.cosmos");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Ghost("ghost", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Middle("", "Frame", &Ghost);
        FakeElement Deep("", "Text", &Middle);
        FakeElement Live("live", "Frame", &Ghost);
        FakeElement UnderLive("", "Text", &Live);

        std::vector<ResolveDiagnostic> Diagnostics;
        auto PointerEventsOf = [&](const FakeElement& Element) {
            return ResolveStyle(Element, StylesheetSet{nullptr, &*Sheet}, Diagnostics).PointerEventsMode;
        };

        ASSERT_TRUE(PointerEventsOf(Deep) == PointerEvents::None);
        ASSERT_TRUE(PointerEventsOf(Live) == PointerEvents::Auto);
        ASSERT_TRUE(PointerEventsOf(UnderLive) == PointerEvents::Auto);
        ASSERT_TRUE(Diagnostics.empty());
    });

    IT("position, the offsets and z-index do not inherit", {
        const auto Sheet = ParseOrFail(".modal { position: fixed; inset: 0; z-index: 100; }", "test.cosmos");
        REQUIRE_TRUE(Sheet.has_value());

        FakeElement Modal("modal", "Frame", nullptr, /*ComponentRoot=*/true);
        FakeElement Child("", "Frame", &Modal);

        std::vector<ResolveDiagnostic> Diagnostics;
        const ResolvedStyle Style = ResolveStyle(Child, StylesheetSet{nullptr, &*Sheet}, Diagnostics);

        ASSERT_FALSE(Style.PositionMode.has_value());
        ASSERT_FALSE(Style.Top.has_value());
        ASSERT_FALSE(Style.ZIndex.has_value());
    });
});
