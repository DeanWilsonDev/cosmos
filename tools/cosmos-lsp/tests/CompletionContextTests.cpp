#include "cimmerian/test.hpp"

#include "CompletionContext.h"
#include "Cosmos/Parser.h"

using namespace CosmosLsp;

namespace {
// clang-format off
const std::string kSource =
    ":root {\n"                                       // line 1
    "    --color-primary: #E8593C;\n"                 // line 2
    "}\n"                                              // line 3
    "\n"                                               // line 4
    ".card {\n"                                        // line 5
    "    --local: 4px;\n"                              // line 6
    "\n"                                               // line 7
    "    padding: 16px;\n"                             // line 8
    "    background-color: var(--color-primary);\n"    // line 9
    "\n"                                                // line 10
    "    .card-title {\n"                              // line 11
    "        font-size: 20px;\n"                        // line 12
    "    }\n"                                           // line 13
    "}\n";                                              // line 14
// clang-format on
} // namespace

DESCRIBE("CompletionContext", {
    IT("classifies the blank line between top-level rules as Statement at depth 0", {
        const CompletionContext Ctx = ClassifyCompletionContext(kSource, 4, 1);
        ASSERT_TRUE(Ctx.Kind == CompletionContextKind::Statement);
        ASSERT_EQUAL(Ctx.Depth, 0);
    });

    IT("classifies the blank line inside .card's block as Statement at depth 1", {
        const CompletionContext Ctx = ClassifyCompletionContext(kSource, 7, 1);
        ASSERT_TRUE(Ctx.Kind == CompletionContextKind::Statement);
        ASSERT_EQUAL(Ctx.Depth, 1);
    });

    IT("classifies right after a property's `:` as Value, with the property name captured", {
        const CompletionContext Ctx = ClassifyCompletionContext(kSource, 9, 23); // just after "background-color:"
        ASSERT_TRUE(Ctx.Kind == CompletionContextKind::Value);
        ASSERT_TRUE(Ctx.Property == "background-color");
    });

    IT("classifies inside an unclosed var( as VarRef", {
        const CompletionContext Ctx = ClassifyCompletionContext(kSource, 9, 28); // just after "var("
        ASSERT_TRUE(Ctx.Kind == CompletionContextKind::VarRef);
    });

    IT("finds the VariableName token inside a var(--name) reference", {
        const auto Token = TokenAtPosition(kSource, 9, 35); // inside "--color-primary"
        REQUIRE_TRUE(Token.has_value());
        ASSERT_TRUE(Token->Kind == Cosmos::TokenKind::VariableName);
        ASSERT_TRUE(Token->Text == "color-primary");
    });

    IT("collects :root's and a top-level rule's own variables, not a nested rule's", {
        Cosmos::Parser              P(kSource, "test.cosmos");
        const Cosmos::ParseResult   Result = P.Parse();
        REQUIRE_TRUE(Result.Errors.empty());
        REQUIRE_TRUE(Result.Sheet.has_value());

        const auto Vars = CollectInScopeVariables(*Result.Sheet);
        REQUIRE_EQUAL(Vars.size(), static_cast<std::size_t>(2));
        ASSERT_TRUE(Vars[0]->Name == "color-primary");
        ASSERT_TRUE(Vars[1]->Name == "local");
    });

    IT("offers property names and primitive selectors inside a block, not var names", {
        const CompletionContext Ctx = ClassifyCompletionContext(kSource, 7, 1);
        ASSERT_TRUE(Ctx.Kind == CompletionContextKind::Statement);
        ASSERT_FALSE(kPropertyNames.empty());
        ASSERT_FALSE(kPrimitiveSelectorNames.empty());
    });

    IT("PropertyValueKeywords returns the closed enum for display, empty for background-color", {
        const auto DisplayValues = PropertyValueKeywords("display");
        ASSERT_EQUAL(DisplayValues.size(), static_cast<std::size_t>(2));
        ASSERT_TRUE(PropertyValueKeywords("background-color").empty());
    });
});
