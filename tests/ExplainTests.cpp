#include "cimmerian/test.hpp"

#include "Cosmos/Explain.h"
#include "Cosmos/Parser.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace Cosmos;

namespace {

class Node : public IStyleTarget {
public:
    Node(std::string Tag, std::string Classes, const Node* Parent, bool Root)
        : Tag_(std::move(Tag)), Classes_(std::move(Classes)), Parent_(Parent), Root_(Root) {}

    std::string         ClassName() const override { return Classes_; }
    std::string         PrimitiveTag() const override { return Tag_; }
    bool                IsComponentRoot() const override { return Root_; }
    const IStyleTarget* Parent() const override { return Parent_; }

private:
    std::string Tag_;
    std::string Classes_;
    const Node* Parent_;
    bool        Root_;
};

const std::string kGlobal = ":root { --brand: #4a90ff; --body: 14px; }\n"
                            "frame { font-size: var(--body); }\n"
                            "text { color: #000000; }\n";

const std::string kCard = ".card {\n"
                          "    --accent: var(--brand);\n"
                          "    padding: 8px;\n"
                          "    .title { color: var(--accent); colr: red; }\n"
                          "    text:hover { color: #ff0000; }\n"
                          "}\n"
                          ".title { gap: 4px; }\n";

struct Sheets {
    ParseResult Global;
    ParseResult Card;

    Sheets() {
        Parser GlobalParser(kGlobal, "global.cosmos");
        Global = GlobalParser.Parse();
        Parser CardParser(kCard, "Card.cosmos");
        Card = CardParser.Parse();
    }

    StylesheetSet Set() const { return StylesheetSet{&*Global.Sheet, &*Card.Sheet}; }
};

std::vector<std::string> RuleLines(const StyleExplanation& Explanation) {
    std::vector<std::string> Lines;
    for (const ExplainedRule& R : Explanation.Rules) {
        Lines.push_back(R.Lineage.back()->Location.FilePath + ":" + std::to_string(R.Lineage.back()->Location.Line) +
                        " " + SelectorChainText(R.Lineage));
        for (const ExplainedDeclaration& D : R.Declarations) {
            std::string Line = "  " + D.Written;
            if (!D.Resolved.empty()) {
                Line += " = " + D.Resolved;
            }
            if (!D.Problem.empty()) {
                Line += " ignored";
            }
            if (D.OverriddenBy) {
                Line += " overridden by " + std::to_string(D.OverriddenBy->Location.Line);
            }
            Lines.push_back(Line);
        }
    }
    return Lines;
}

} // namespace

DESCRIBE("Explain", {
    IT("lists the matching rules in cascade order, with what each declaration does", {
        const Sheets      S;
        const Node        Card("Frame", "card", nullptr, true);
        const Node        Title("Text", "title", &Card, false);
        const StyleExplanation Explanation = ExplainStyle(Title, S.Set());
        ASSERT_EQUAL(RuleLines(Explanation),
                     (std::vector<std::string>{"global.cosmos:3 text", "  color: #000000 overridden by 4",
                                               "Card.cosmos:7 .title", "  gap: 4px ignored",
                                               "Card.cosmos:4 .card .title", "  color: var(--accent) = #4a90ff",
                                               "  colr: red ignored", "Card.cosmos:5 .card text:hover",
                                               "  color: #ff0000"}));
    });

    IT("gives the resolved style and names what's inherited", {
        const Sheets           S;
        const Node             Card("Frame", "card", nullptr, true);
        const Node             Title("Text", "title", &Card, false);
        const StyleExplanation Explanation = ExplainStyle(Title, S.Set());
        std::vector<std::string> Described;
        for (const StyleProperty& Property : DescribeStyle(Explanation.Style)) {
            Described.push_back(Property.Name + ": " + Property.Value);
        }
        ASSERT_EQUAL(Described, (std::vector<std::string>{"color: #4a90ff", "font-size: 14px"}));
        ASSERT_EQUAL(Explanation.InheritedProperties, std::vector<std::string>{"font-size"});
        REQUIRE_TRUE(Explanation.Style.Hover != nullptr);
        ASSERT_EQUAL(DescribeStyle(*Explanation.Style.Hover)[0].Value, std::string("#ff0000"));
    });

    IT("treats inset as setting each offset, for overriding", {
        const std::string Source = ".box { top: 4px; inset: 0px; }\n";
        Parser            P(Source, "Box.cosmos");
        const ParseResult Parsed = P.Parse();
        const Node        Box("Frame", "box", nullptr, true);
        const StyleExplanation Explanation = ExplainStyle(Box, StylesheetSet{nullptr, &*Parsed.Sheet});
        REQUIRE_EQUAL(Explanation.Rules.size(), static_cast<std::size_t>(1));
        ASSERT_TRUE(Explanation.Rules[0].Declarations[0].OverriddenBy != nullptr);
    });
});
