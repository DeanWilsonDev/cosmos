#include "cimmerian/test.hpp"

#include "Navigation.h"

using namespace CosmosLsp;

namespace {

const std::string kSheet = ":root {\n"
                           "    --accent: #4a90ff;\n"
                           "}\n"
                           ".card {\n"
                           "    background-color: var(--accent);\n"
                           "    .title { color: var(--accent); }\n"
                           "}\n"
                           ".card:hover { padding: 4px; }\n";

} // namespace

DESCRIBE("Navigation", {
    IT("names the variable, class or property under the cursor", {
        const auto Declared = SymbolAt(kSheet, 2, 7);
        REQUIRE_TRUE(Declared.has_value());
        ASSERT_TRUE(Declared->Kind == NamedSymbolKind::Variable);
        ASSERT_EQUAL(Declared->Name, std::string("accent"));
        ASSERT_TRUE(Declared->IsDeclaration);
        ASSERT_EQUAL(Declared->Span, (TextSpan{2, 5, 2, 13}));

        const auto Used = SymbolAt(kSheet, 5, 32);
        REQUIRE_TRUE(Used.has_value());
        ASSERT_FALSE(Used->IsDeclaration);

        const auto Class = SymbolAt(kSheet, 6, 6);
        REQUIRE_TRUE(Class.has_value());
        ASSERT_TRUE(Class->Kind == NamedSymbolKind::Class);
        ASSERT_EQUAL(Class->Name, std::string("title"));

        const auto Property = SymbolAt(kSheet, 5, 8);
        REQUIRE_TRUE(Property.has_value());
        ASSERT_TRUE(Property->Kind == NamedSymbolKind::Property);
        ASSERT_EQUAL(Property->Name, std::string("background-color"));

        ASSERT_FALSE(SymbolAt(kSheet, 8, 24).has_value());
    });

    IT("finds a variable's declaration and every var() of it", {
        const auto Found = VariableOccurrences(kSheet, "accent");
        REQUIRE_EQUAL(Found.size(), static_cast<std::size_t>(3));
        ASSERT_TRUE(Found[0].IsDeclaration);
        ASSERT_FALSE(Found[1].IsDeclaration);
        ASSERT_EQUAL(Found[2].Span, (TextSpan{6, 25, 6, 33}));
    });

    IT("finds every selector for a class", {
        const auto Found = ClassSelectorOccurrences(kSheet, "card");
        REQUIRE_EQUAL(Found.size(), static_cast<std::size_t>(2));
        ASSERT_EQUAL(Found[1].Span, (TextSpan{8, 1, 8, 6}));
    });

    IT("finds a class in .chaos class strings, word by word", {
        const std::string Chaos = "<Frame class=\"card wide\">\n"
                                  "  <Text class='card-title'>{title}</Text>\n"
                                  "  <Frame class = \"wide card\" subclass=\"card\" />\n"
                                  "</Frame>\n";
        const auto Found = ClassUsesInChaos(Chaos, "card");
        REQUIRE_EQUAL(Found.size(), static_cast<std::size_t>(2));
        ASSERT_EQUAL(Found[0].Span, (TextSpan{1, 15, 1, 19}));
        ASSERT_EQUAL(Found[1].Span, (TextSpan{3, 24, 3, 28}));

        const auto At = ChaosClassAt(Chaos, 2, 20);
        REQUIRE_TRUE(At.has_value());
        ASSERT_EQUAL(At->Name, std::string("card-title"));
        ASSERT_FALSE(ChaosClassAt(Chaos, 2, 33).has_value());
    });
});
