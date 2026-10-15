#include "cimmerian/test.hpp"

#include "Cosmos/Parser.h"

using namespace Cosmos;

DESCRIBE("Parser", {
    IT("parses the HealthBar worked example (docs/cosmos_core_spec.md §4)", {
        const std::string Source = R"(
.health-bar {
    --bar-background: #333333;

    width: 200px;
    height: 16px;
    background-color: var(--bar-background);
    border-radius: 8px;
}

.bar-normal {
    background-color: #4CAF50;
}

.bar-critical {
    background-color: #E8593C;
}
)";
        Parser TheParser(Source, "HealthBar.cosmos");
        const ParseResult Result = TheParser.Parse();

        ASSERT_TRUE(Result.Errors.empty());
        REQUIRE_TRUE(Result.Sheet.has_value());
        REQUIRE_EQUAL(Result.Sheet->Rules.size(), static_cast<std::size_t>(3));

        const Rule& HealthBar = *Result.Sheet->Rules[0];
        ASSERT_TRUE(HealthBar.Kind == SelectorKind::Class && HealthBar.ClassName == "health-bar");
        ASSERT_EQUAL(HealthBar.Variables.size(), static_cast<std::size_t>(1));
        ASSERT_TRUE(HealthBar.Variables[0].Name == "bar-background");
        ASSERT_EQUAL(HealthBar.Declarations.size(), static_cast<std::size_t>(4));
    });

    IT("parses nested descendant selectors and a pseudo-class suffix", {
        const std::string Source = R"(
.card {
    padding: 16px;

    .card-title {
        font-size: 20px;
    }
}

.card:hover {
    .card-title {
        color: var(--color-text-emphasis);
    }
}
)";
        Parser TheParser(Source, "Card.cosmos");
        const ParseResult Result = TheParser.Parse();

        ASSERT_TRUE(Result.Errors.empty());
        REQUIRE_TRUE(Result.Sheet.has_value());
        REQUIRE_EQUAL(Result.Sheet->Rules.size(), static_cast<std::size_t>(2));

        const Rule& Card = *Result.Sheet->Rules[0];
        ASSERT_FALSE(Card.Pseudo.has_value());
        REQUIRE_EQUAL(Card.NestedRules.size(), static_cast<std::size_t>(1));
        ASSERT_TRUE(Card.NestedRules[0]->ClassName == "card-title");
        ASSERT_TRUE(Card.NestedRules[0]->Depth == 1);

        const Rule& CardHover = *Result.Sheet->Rules[1];
        REQUIRE_TRUE(CardHover.Pseudo.has_value());
        ASSERT_TRUE(*CardHover.Pseudo == PseudoKind::Hover);
    });

    IT("parses a :root block", {
        const std::string Source = R"(
:root {
    --color-primary: #E8593C;
    --spacing-md: 16px;
}
)";
        Parser TheParser(Source, "global.cosmos");
        const ParseResult Result = TheParser.Parse();

        ASSERT_TRUE(Result.Errors.empty());
        REQUIRE_TRUE(Result.Sheet.has_value());
        REQUIRE_TRUE(Result.Sheet->Root.has_value());
        ASSERT_EQUAL(Result.Sheet->Root->Variables.size(), static_cast<std::size_t>(2));
    });

    IT("rejects a compound tag+class selector", {
        Parser TheParser("Frame.card { padding: 16px; }", "test.cosmos");
        const ParseResult Result = TheParser.Parse();

        ASSERT_FALSE(Result.Errors.empty());
        ASSERT_TRUE(Result.Errors[0].Message.find("Compound") != std::string::npos);
    });

    IT("rejects a duplicate selector in one file", {
        const std::string Source = R"(
.card { padding: 16px; }
.card { margin: 8px; }
)";
        Parser TheParser(Source, "test.cosmos");
        const ParseResult Result = TheParser.Parse();

        ASSERT_FALSE(Result.Errors.empty());
        ASSERT_TRUE(Result.Errors[0].Message.find("Duplicate selector") != std::string::npos);
    });

    IT("parses a primitive selector and transform's scale(...) call value", {
        Parser TheParser("frame { transform: scale(1.5); }", "test.cosmos");
        const ParseResult Result = TheParser.Parse();

        ASSERT_TRUE(Result.Errors.empty());
        REQUIRE_TRUE(Result.Sheet.has_value());
        REQUIRE_EQUAL(Result.Sheet->Rules.size(), static_cast<std::size_t>(1));
        const Rule& Frame = *Result.Sheet->Rules[0];
        ASSERT_TRUE(Frame.Kind == SelectorKind::Primitive && Frame.PrimitiveName == "frame");
        REQUIRE_EQUAL(Frame.Declarations.size(), static_cast<std::size_t>(1));
        REQUIRE_EQUAL(Frame.Declarations[0].Values.size(), static_cast<std::size_t>(1));
        ASSERT_TRUE(Frame.Declarations[0].Values[0].Literal == "scale");
        ASSERT_TRUE(Frame.Declarations[0].Values[0].CallArgument == "1.5");
    });

    IT("marks the values that follow a comma", {
        Parser TheParser(".bar { transition: width 0.2s, height 0.4s; }", "test.cosmos");
        const ParseResult Result = TheParser.Parse();

        ASSERT_TRUE(Result.Errors.empty());
        REQUIRE_TRUE(Result.Sheet.has_value());
        const std::vector<ValuePart>& Values = Result.Sheet->Rules[0]->Declarations[0].Values;
        REQUIRE_EQUAL(Values.size(), static_cast<std::size_t>(4));
        ASSERT_FALSE(Values[0].FollowsComma);
        ASSERT_FALSE(Values[1].FollowsComma);
        ASSERT_TRUE(Values[2].FollowsComma);
        ASSERT_FALSE(Values[3].FollowsComma);
    });

    IT("parses a nested @starting-style block onto its rule, not as a nested rule", {
        Parser TheParser(".bar { flex-grow: 60; @starting-style { flex-grow: 0; opacity: 0; } }", "test.cosmos");
        const ParseResult Result = TheParser.Parse();

        ASSERT_TRUE(Result.Errors.empty());
        REQUIRE_TRUE(Result.Sheet.has_value());
        const Rule& Bar = *Result.Sheet->Rules[0];
        ASSERT_EQUAL(Bar.Declarations.size(), static_cast<std::size_t>(1));
        ASSERT_TRUE(Bar.NestedRules.empty());
        REQUIRE_EQUAL(Bar.StartingDeclarations.size(), static_cast<std::size_t>(2));
        ASSERT_TRUE(Bar.StartingDeclarations[0].Property == "flex-grow");
        ASSERT_TRUE(Bar.StartingDeclarations[1].Property == "opacity");
    });

    IT("reports an unknown at-rule and keeps parsing", {
        Parser TheParser(".bar { @keyframes { width: 0px; } color: #FFFFFF; }", "test.cosmos");
        const ParseResult Result = TheParser.Parse();

        REQUIRE_EQUAL(Result.Errors.size(), static_cast<std::size_t>(1));
        ASSERT_TRUE(Result.Errors[0].Message.find("Unknown at-rule `@keyframes`") != std::string::npos);
        REQUIRE_TRUE(Result.Sheet.has_value());
        const Rule& Bar = *Result.Sheet->Rules[0];
        ASSERT_TRUE(Bar.StartingDeclarations.empty());
        ASSERT_EQUAL(Bar.Declarations.size(), static_cast<std::size_t>(1));
    });

    IT("parses attr(name) into an AttrRef", {
        Parser TheParser(".bar { flex-grow: attr(fill); }", "test.cosmos");
        const ParseResult Result = TheParser.Parse();

        ASSERT_TRUE(Result.Errors.empty());
        REQUIRE_TRUE(Result.Sheet.has_value());
        const std::vector<ValuePart>& Values = Result.Sheet->Rules[0]->Declarations[0].Values;
        REQUIRE_EQUAL(Values.size(), static_cast<std::size_t>(1));
        ASSERT_TRUE(Values[0].AttrRef == "fill");
        ASSERT_FALSE(Values[0].Literal.has_value());
    });
});
