#include "cimmerian/test.hpp"

#include "cosmos/Checker.h"
#include "cosmos/Parser.h"

#include <string>
#include <vector>

using namespace Cosmos;

namespace {

const std::string kGlobal = ":root {\n"
                            "    --accent: #4a90ff;\n"
                            "    --pad: 8px;\n"
                            "    --alias: var(--accent);\n"
                            "}\n";

struct Checked {
    std::vector<CheckDiagnostic> Diagnostics;

    const CheckDiagnostic* At(std::uint32_t Line) const {
        for (const CheckDiagnostic& D : Diagnostics) {
            if (D.Location.Line == Line) {
                return &D;
            }
        }
        return nullptr;
    }
};

Checked CheckComponent(const std::string& Source, const std::string* Global = &kGlobal) {
    Parser      ComponentParser(Source, "Card.cosmos");
    ParseResult Component = ComponentParser.Parse();
    std::optional<ParseResult> GlobalResult;
    if (Global) {
        Parser GlobalParser(*Global, "global.cosmos");
        GlobalResult = GlobalParser.Parse();
    }
    StylesheetSet Sheets{GlobalResult ? &*GlobalResult->Sheet : nullptr, &*Component.Sheet};
    return Checked{CheckStylesheet(Sheets)};
}

} // namespace

DESCRIBE("Checker", {
    IT("finds nothing in a clean stylesheet", {
        const Checked Result = CheckComponent(".card {\n"
                                              "    display: stack;\n"
                                              "    padding: var(--pad) 16px;\n"
                                              "    background-color: var(--alias);\n"
                                              "    text { color: #fff; }\n"
                                              "}\n");
        ASSERT_TRUE(Result.Diagnostics.empty());
    });

    IT("warns about an unknown property, over its name, with the closest one", {
        const Checked Result = CheckComponent(".card {\n    colr: #fff;\n}\n");
        REQUIRE_EQUAL(Result.Diagnostics.size(), static_cast<std::size_t>(1));
        const CheckDiagnostic& D = Result.Diagnostics[0];
        ASSERT_TRUE(D.Severity == CheckSeverity::Warning);
        ASSERT_EQUAL(D.Message, std::string("Unknown property `colr` -- Cosmos ignores it. Did you mean `color`?"));
        ASSERT_EQUAL(D.Suggestion, std::string("color"));
        ASSERT_EQUAL(D.Location.Line, static_cast<std::uint32_t>(2));
        ASSERT_EQUAL(D.Location.Column, static_cast<std::uint32_t>(5));
        ASSERT_EQUAL(D.End.Column, static_cast<std::uint32_t>(9));
    });

    IT("reports an undefined variable over its var(), with the closest name", {
        const Checked Result = CheckComponent(".card {\n    padding: var(--padd);\n}\n");
        REQUIRE_EQUAL(Result.Diagnostics.size(), static_cast<std::size_t>(1));
        const CheckDiagnostic& D = Result.Diagnostics[0];
        ASSERT_TRUE(D.Severity == CheckSeverity::Error);
        ASSERT_TRUE(D.Message.find("Undefined variable `var(--padd)`") == 0);
        ASSERT_TRUE(D.Message.find("Did you mean `--pad`?") != std::string::npos);
        ASSERT_EQUAL(D.Suggestion, std::string("var(--pad)"));
        ASSERT_EQUAL(D.Location.Column, static_cast<std::uint32_t>(14));
        ASSERT_EQUAL(D.End.Column, static_cast<std::uint32_t>(25));
    });

    IT("sees variables declared anywhere in the component's own file", {
        const Checked Result = CheckComponent(".card {\n"
                                              "    .title { --title-size: 18px; }\n"
                                              "    font-size: var(--title-size);\n"
                                              "}\n");
        ASSERT_TRUE(Result.Diagnostics.empty());
    });

    IT("skips undefined variables when there's no global sheet", {
        const Checked Result = CheckComponent(".card {\n    padding: var(--pad);\n    colr: #fff;\n}\n", nullptr);
        REQUIRE_EQUAL(Result.Diagnostics.size(), static_cast<std::size_t>(1));
        ASSERT_TRUE(Result.Diagnostics[0].Severity == CheckSeverity::Warning);
    });

    IT("reports an undefined variable inside another variable's value", {
        const Checked Result = CheckComponent(".card {\n    --tint: var(--missing);\n}\n");
        REQUIRE_EQUAL(Result.Diagnostics.size(), static_cast<std::size_t>(1));
        ASSERT_TRUE(Result.Diagnostics[0].Message.find("`var(--missing)`") != std::string::npos);
    });

    IT("reports values the resolver drops, over the whole declaration", {
        const Checked Result = CheckComponent(".card {\n"
                                              "    color: 12px;\n"
                                              "    border-width: 10%;\n"
                                              "    font-family: body;\n"
                                              "    opacity: 2px;\n"
                                              "}\n");
        REQUIRE_EQUAL(Result.Diagnostics.size(), static_cast<std::size_t>(4));
        ASSERT_TRUE(Result.At(2)->Message.find("`color` takes a colour") != std::string::npos);
        ASSERT_TRUE(Result.At(3)->Message.find("border-width: 10%") != std::string::npos);
        ASSERT_TRUE(Result.At(4)->Message.find("`font-family` takes a font file's path") != std::string::npos);
        ASSERT_TRUE(Result.At(5)->Message.find("`opacity` takes a number from 0 to 1") != std::string::npos);
        ASSERT_EQUAL(Result.At(2)->Location.Column, static_cast<std::uint32_t>(5));
        ASSERT_EQUAL(Result.At(2)->End.Column, static_cast<std::uint32_t>(16));
    });

    IT("offers the closest keyword over the value alone", {
        const Checked Result = CheckComponent(".card {\n    align-items: centre;\n}\n");
        REQUIRE_EQUAL(Result.Diagnostics.size(), static_cast<std::size_t>(1));
        ASSERT_EQUAL(Result.Diagnostics[0].Suggestion, std::string("center"));
        ASSERT_EQUAL(Result.Diagnostics[0].Location.Column, static_cast<std::uint32_t>(18));
        ASSERT_EQUAL(Result.Diagnostics[0].End.Column, static_cast<std::uint32_t>(24));
    });

    IT("reports a container-only property under a primitive selector for a leaf", {
        const Checked Result = CheckComponent(".card {\n"
                                              "    text { gap: 4px; }\n"
                                              "    frame { gap: 4px; }\n"
                                              "}\n"
                                              "image { display: stack; }\n"
                                              ".label { display: stack; }\n");
        REQUIRE_EQUAL(Result.Diagnostics.size(), static_cast<std::size_t>(2));
        ASSERT_TRUE(Result.At(2)->Message.find("`gap` has no effect on `Text`") == 0);
        ASSERT_TRUE(Result.At(5)->Message.find("`display` has no effect on `Image`") == 0);
    });

    IT("reports @starting-style inside a pseudo-class rule", {
        const Checked Result = CheckComponent(".card:hover {\n    @starting-style { opacity: 0; }\n}\n");
        REQUIRE_EQUAL(Result.Diagnostics.size(), static_cast<std::size_t>(1));
        ASSERT_TRUE(Result.Diagnostics[0].Message.find("`@starting-style` has no effect inside a `:hover` rule") == 0);
    });

    IT("checks @starting-style's own declarations", {
        const Checked Result = CheckComponent(".card {\n    @starting-style { opacity: 2px; }\n}\n");
        REQUIRE_EQUAL(Result.Diagnostics.size(), static_cast<std::size_t>(1));
        ASSERT_TRUE(Result.Diagnostics[0].Message.find("`opacity`") != std::string::npos);
    });

    IT("checks global.cosmos on its own, with only its :root in scope", {
        const std::string Global = ":root { --accent: #4a90ff; }\n"
                                   ".panel { background-color: var(--accent); color: var(--ink); }\n";
        Parser      P(Global, "global.cosmos");
        ParseResult Parsed = P.Parse();
        const std::vector<CheckDiagnostic> Found = CheckStylesheet(StylesheetSet{&*Parsed.Sheet, nullptr});
        REQUIRE_EQUAL(Found.size(), static_cast<std::size_t>(1));
        ASSERT_TRUE(Found[0].Message.find("`var(--ink)`") != std::string::npos);
    });

    IT("orders diagnostics by position", {
        const Checked Result = CheckComponent(".b {\n    colr: #fff;\n}\n.a {\n    widht: 4px;\n    color: 1px;\n}\n");
        REQUIRE_EQUAL(Result.Diagnostics.size(), static_cast<std::size_t>(3));
        ASSERT_EQUAL(Result.Diagnostics[0].Location.Line, static_cast<std::uint32_t>(2));
        ASSERT_EQUAL(Result.Diagnostics[1].Location.Line, static_cast<std::uint32_t>(5));
        ASSERT_EQUAL(Result.Diagnostics[2].Location.Line, static_cast<std::uint32_t>(6));
    });

    IT("reports a variable that refers to itself instead of looping", {
        const Checked Result = CheckComponent(".card {\n    --a: var(--a);\n    color: var(--a);\n}\n");
        REQUIRE_EQUAL(Result.Diagnostics.size(), static_cast<std::size_t>(2));
        ASSERT_EQUAL(Result.At(2)->Message, std::string("Variable `--a` refers to itself: `--a` -> `--a`."));
        ASSERT_TRUE(Result.At(3)->Message.find("refers to itself") != std::string::npos);
    });

    IT("reports a cycle through global.cosmos at the use in this file", {
        const std::string Global = ":root {\n    --a: var(--b);\n    --b: var(--a);\n}\n";
        const Checked     Result = CheckComponent(".card {\n    color: var(--a);\n}\n", &Global);
        REQUIRE_EQUAL(Result.Diagnostics.size(), static_cast<std::size_t>(1));
        ASSERT_EQUAL(Result.Diagnostics[0].Location.FilePath, std::string("Card.cosmos"));
        ASSERT_EQUAL(Result.Diagnostics[0].Message,
                     std::string("Variable `--a` refers to itself: `--a` -> `--b` -> `--a`."));
    });

    IT("follows a chain of any length, as the resolver does", {
        const std::string Source = ".card {\n    --x: var(--y);\n    --y: var(--z);\n    --z: #ffffff;\n    color: var(--x);\n}\n";
        ASSERT_TRUE(CheckComponent(Source, nullptr).Diagnostics.empty());
        ASSERT_TRUE(CheckComponent(Source).Diagnostics.empty());
    });

    IT("reports a name missing behind a global variable at the use in this file", {
        const std::string Global = ":root {\n    --a: var(--missing);\n}\n";
        const Checked     Result = CheckComponent(".card {\n    color: var(--a);\n}\n", &Global);
        REQUIRE_EQUAL(Result.Diagnostics.size(), static_cast<std::size_t>(1));
        const CheckDiagnostic& D = Result.Diagnostics[0];
        ASSERT_EQUAL(D.Location.FilePath, std::string("Card.cosmos"));
        ASSERT_EQUAL(D.Location.Line, static_cast<std::uint32_t>(2));
        ASSERT_TRUE(D.Message.find("`var(--missing)`") != std::string::npos);
        ASSERT_TRUE(D.Message.find("reached through `var(--a)`") != std::string::npos);
        ASSERT_TRUE(D.Suggestion.empty());
    });
});

