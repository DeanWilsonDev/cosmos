#include "cimmerian/test.hpp"

#include "cosmos/Formatter.h"

#include <string>

using namespace Cosmos;

namespace {

std::string Formatted(const std::string& Source, const FormatOptions& Options = {}) {
    const FormatResult Result = FormatSource(Source, "Test.cosmos", Options);
    return Result.Outcome == FormatOutcome::Formatted ? Result.Formatted : "<" + Result.Problem + ">";
}

} // namespace

DESCRIBE("Formatter", {
    IT("puts each declaration on its own line, indented, with a blank line between top-level blocks", {
        ASSERT_EQUAL(Formatted(":root{--brand:#4a90ff;--gap:   8px;}\n.card{padding: 8px 16px;text:hover{color: "
                               "rgb(1,2,3);}}\n.empty {}\n"),
                     std::string(":root {\n"
                                 "    --brand: #4a90ff;\n"
                                 "    --gap: 8px;\n"
                                 "}\n"
                                 "\n"
                                 ".card {\n"
                                 "    padding: 8px 16px;\n"
                                 "    text:hover {\n"
                                 "        color: rgb(1, 2, 3);\n"
                                 "    }\n"
                                 "}\n"
                                 "\n"
                                 ".empty {}\n"));
    });

    IT("keeps comments where they were, and at most one blank line the author left", {
        ASSERT_EQUAL(Formatted("/* top */\n.a { gap: 1px; /* why */\n\n\n\n  color: #fff;\n  /* own line */\n}\n/* "
                               "next */\n.b { gap: 2px; }\n"),
                     std::string("/* top */\n"
                                 ".a {\n"
                                 "    gap: 1px; /* why */\n"
                                 "\n"
                                 "    color: #fff;\n"
                                 "    /* own line */\n"
                                 "}\n"
                                 "\n"
                                 "/* next */\n"
                                 ".b {\n"
                                 "    gap: 2px;\n"
                                 "}\n"));
    });

    IT("splits a comma-separated value that's too long, one group per line", {
        const std::string Source = ".a { transition: background-color 0.2s ease-in-out, opacity 0.4s linear; }\n";
        ASSERT_EQUAL(Formatted(Source, FormatOptions{.LineWidth = 40, .IndentWidth = 2}),
                     std::string(".a {\n"
                                 "  transition:\n"
                                 "    background-color 0.2s ease-in-out,\n"
                                 "    opacity 0.4s linear;\n"
                                 "}\n"));
        ASSERT_EQUAL(Formatted(Source),
                     std::string(".a {\n    transition: background-color 0.2s ease-in-out, opacity 0.4s linear;\n}\n"));
    });

    IT("formats @starting-style and gives the same text a second time", {
        const std::string Once = Formatted(".a { opacity: 1; @starting-style { opacity: 0; } }");
        ASSERT_EQUAL(Once, std::string(".a {\n    opacity: 1;\n    @starting-style {\n        opacity: 0;\n    }\n}\n"));
        ASSERT_EQUAL(Formatted(Once), Once);
    });

    IT("leaves a file that doesn't parse alone", {
        const FormatResult Result = FormatSource(".a { gap: 1px", "Broken.cosmos");
        ASSERT_TRUE(Result.Outcome == FormatOutcome::SyntaxErrors);
        ASSERT_FALSE(Result.Errors.empty());
        ASSERT_TRUE(Result.Formatted.empty());
    });

    IT("formats an empty file as empty", { ASSERT_EQUAL(Formatted(""), std::string()); });
});
