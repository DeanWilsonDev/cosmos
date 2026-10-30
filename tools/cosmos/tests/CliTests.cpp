#include "cimmerian/test.hpp"

#include "CosmosCli.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

using namespace CosmosCli;

namespace {

class ScratchFolder {
public:
    ScratchFolder() {
        static int Counter = 0;
        Path_ = std::filesystem::temp_directory_path() /
                ("cosmos-cli-tests-" + std::to_string(::getpid()) + "-" + std::to_string(++Counter));
        std::filesystem::create_directories(Path_);
    }
    ~ScratchFolder() { std::filesystem::remove_all(Path_); }

    void Write(const std::string& Name, const std::string& Text) const {
        std::filesystem::create_directories((Path_ / Name).parent_path());
        std::ofstream(Path_ / Name) << Text;
    }
    std::string Path(const std::string& Name = "") const { return (Path_ / Name).string(); }

private:
    std::filesystem::path Path_;
};

struct Run {
    int         Exit;
    std::string Out;
    std::string Err;
};

std::string WithoutPrefix(std::string Text, const std::string& Prefix) {
    for (std::size_t At = Text.find(Prefix); At != std::string::npos; At = Text.find(Prefix, At)) {
        Text.erase(At, Prefix.size());
    }
    return Text;
}

Run RunCli(const std::vector<std::string>& Arguments, const ScratchFolder* Folder = nullptr) {
    std::ostringstream Out;
    std::ostringstream Err;
    const int          Exit   = RunCosmosCli(Arguments, Out, Err);
    const std::string  Prefix = Folder ? Folder->Path("") : std::string("\x01");
    return Run{Exit, WithoutPrefix(Out.str(), Prefix), WithoutPrefix(Err.str(), Prefix)};
}

const std::string kGlobal = ":root {\n"
                            "    --brand: #4a90ff;\n"
                            "    --body: 14px;\n"
                            "}\n"
                            "frame { font-size: var(--body); }\n";

const std::string kCard = ".card {\n"
                          "    colr: #ffffff;\n"
                          "    background-color: var(--brnd);\n"
                          "    text:hover { color: var(--brand); }\n"
                          "}\n"
                          ".title { color: #000000; width: attr(w); }\n";

} // namespace

DESCRIBE("cosmos command line", {
    IT("prints its usage for help, and refuses an unknown command", {
        const Run Help = RunCli({"help"});
        ASSERT_EQUAL(Help.Exit, kExitSuccess);
        ASSERT_TRUE(Help.Out.starts_with("usage: cosmos <command> [arguments]\n"));
        const Run Unknown = RunCli({"lint"});
        ASSERT_EQUAL(Unknown.Exit, kExitUsage);
        ASSERT_EQUAL(Unknown.Err, std::string("error: unknown command 'lint'\nrun 'cosmos help' for usage\n"));
        ASSERT_EQUAL(RunCli({}).Exit, kExitUsage);
    });

    IT("checks a folder's stylesheets against the global.cosmos beside them", {
        ScratchFolder Folder;
        Folder.Write("ui/global.cosmos", kGlobal);
        Folder.Write("ui/Card.cosmos", kCard);
        Folder.Write("build/Ignored.cosmos", ".x { colr: red; }\n");
        const Run Checked = RunCli({"check", Folder.Path()}, &Folder);
        ASSERT_EQUAL(Checked.Exit, kExitFailure);
        ASSERT_EQUAL(Checked.Out, std::string());
        ASSERT_EQUAL(Checked.Err,
                     std::string("ui/Card.cosmos:2:5: warning: Unknown property `colr` -- Cosmos ignores it. Did you "
                                 "mean `color`?\n"
                                 " 2 |     colr: #ffffff;\n"
                                 "   |     ^^^^\n"
                                 "ui/Card.cosmos:3:23: error: Undefined variable `var(--brnd)` -- no `--brnd` is "
                                 "declared in global.cosmos's `:root` or in this component's file. Did you mean "
                                 "`--brand`?\n"
                                 " 3 |     background-color: var(--brnd);\n"
                                 "   |                       ^^^^^^^^^^^\n"
                                 "checked 2 files: 1 error, 1 warning\n"));
    });

    IT("exits 0 on warnings alone, and says nothing about clean files", {
        ScratchFolder Folder;
        Folder.Write("global.cosmos", kGlobal);
        Folder.Write("Clean.cosmos", ".clean { color: var(--brand); }\n");
        Folder.Write("Warned.cosmos", ".warned { colr: red; }\n");
        const Run Clean = RunCli({"check", Folder.Path("Clean.cosmos"), Folder.Path("global.cosmos")}, &Folder);
        ASSERT_EQUAL(Clean.Exit, kExitSuccess);
        ASSERT_EQUAL(Clean.Err, std::string());
        const Run Warned = RunCli({"check", Folder.Path("Warned.cosmos")}, &Folder);
        ASSERT_EQUAL(Warned.Exit, kExitSuccess);
        ASSERT_TRUE(Warned.Err.ends_with("checked 1 file: 0 errors, 1 warning\n"));
    });

    IT("reports parse errors and files it can't read", {
        ScratchFolder Folder;
        Folder.Write("Broken.cosmos", ".broken { color: red\n");
        const Run Broken = RunCli({"check", Folder.Path("Broken.cosmos"), Folder.Path("Missing.cosmos")}, &Folder);
        ASSERT_EQUAL(Broken.Exit, kExitFailure);
        ASSERT_TRUE(Broken.Err.starts_with("Broken.cosmos:"));
        ASSERT_TRUE(Broken.Err.find("error: can't read 'Missing.cosmos'\n") != std::string::npos);
    });

    IT("skips files that aren't stylesheets, checks each file once, and says when there's nothing to check", {
        ScratchFolder Folder;
        Folder.Write("Card.cosmos", ".card { colr: red; }\n");
        Folder.Write("Card.chaos", "<frame class=\"card\" />\n");
        Folder.Write("empty/README.md", "nothing here\n");
        const Run Globbed =
            RunCli({"check", Folder.Path("Card.chaos"), Folder.Path("Card.cosmos"), Folder.Path()}, &Folder);
        ASSERT_EQUAL(Globbed.Exit, kExitSuccess);
        ASSERT_TRUE(Globbed.Err.starts_with("note: skipping 'Card.chaos', which isn't a .cosmos file\n"));
        ASSERT_TRUE(Globbed.Err.ends_with("checked 1 file: 0 errors, 1 warning\n"));
        const Run Empty = RunCli({"check", Folder.Path("empty")}, &Folder);
        ASSERT_EQUAL(Empty.Exit, kExitSuccess);
        ASSERT_EQUAL(Empty.Err, std::string("warning: no .cosmos files to check\n"));
    });

    IT("prints tokens with their positions", {
        ScratchFolder Folder;
        Folder.Write("Tiny.cosmos", ".a { gap: 4px; }\n");
        const Run Tokens = RunCli({"tokens", Folder.Path("Tiny.cosmos")}, &Folder);
        ASSERT_EQUAL(Tokens.Exit, kExitSuccess);
        ASSERT_EQUAL(Tokens.Out, std::string("1:1  ClassName  'a'\n"
                                             "1:4  OpenBrace  '{'\n"
                                             "1:6  Identifier  'gap'\n"
                                             "1:9  Colon  ':'\n"
                                             "1:11  Number  '4px'\n"
                                             "1:14  Semicolon  ';'\n"
                                             "1:16  CloseBrace  '}'\n"));
    });

    IT("prints the syntax tree as JSON, with locations on request", {
        ScratchFolder Folder;
        Folder.Write("Tiny.cosmos", ".a { gap: 4px; }\n");
        const Run Plain = RunCli({"ast", Folder.Path("Tiny.cosmos")}, &Folder);
        ASSERT_EQUAL(Plain.Exit, kExitSuccess);
        ASSERT_TRUE(Plain.Out.find("\"class\": \"a\"") != std::string::npos);
        ASSERT_TRUE(Plain.Out.find("\"kind\": \"number\"") != std::string::npos);
        ASSERT_TRUE(Plain.Out.find("\"location\"") == std::string::npos);
        const Run Located = RunCli({"ast", "--locations", Folder.Path("Tiny.cosmos")}, &Folder);
        ASSERT_TRUE(Located.Out.find("\"location\": \"1:11\"") != std::string::npos);
        ASSERT_EQUAL(RunCli({"ast", "--tree", Folder.Path("Tiny.cosmos")}).Exit, kExitUsage);
    });

    IT("explains an element's rules and style", {
        ScratchFolder Folder;
        Folder.Write("global.cosmos", kGlobal);
        Folder.Write("Card.cosmos", kCard);
        const Run Explained = RunCli(
            {"explain", Folder.Path("Card.cosmos"), "Frame[class=\"card\"] > Text[class=\"title\" w=40]:hover"},
            &Folder);
        ASSERT_EQUAL(Explained.Exit, kExitSuccess);
        ASSERT_EQUAL(Explained.Out, std::string("Text[class=\"title\" w=\"40\"]:hover\n"
                                                "  inside Frame[class=\"card\"]  (component root)\n"
                                                "\n"
                                                "Rules, in cascade order:\n"
                                                "  Card.cosmos:6:1  .title\n"
                                                "    color: #000000;\n"
                                                "    width: attr(w);\n"
                                                "  Card.cosmos:4:5  .card text:hover\n"
                                                "    color: var(--brand);  = #4a90ff\n"
                                                "\n"
                                                "Style:\n"
                                                "  color: #000000\n"
                                                "  font-size: 14px  (inherited)\n"
                                                "  width: 40  (from attr(w))\n"
                                                "\n"
                                                "While hovered, on top of the style above:\n"
                                                "  color: #4a90ff\n"));
    });

    IT("takes the component root and the global sheet from options", {
        ScratchFolder Folder;
        Folder.Write("themes/dark.cosmos", ":root { --brand: #000000; }\n");
        Folder.Write("Card.cosmos", ".card {\n    text { color: var(--brand); }\n}\n");
        const Run Rooted = RunCli({"explain", "--root", "2", "--global", Folder.Path("themes/dark.cosmos"),
                                   Folder.Path("Card.cosmos"), "Frame[class=card] > Text"},
                                  &Folder);
        ASSERT_EQUAL(Rooted.Exit, kExitSuccess);
        ASSERT_TRUE(Rooted.Out.find("  (none match)\n") != std::string::npos);
        const Run Unrooted = RunCli({"explain", "--global", Folder.Path("themes/dark.cosmos"),
                                     Folder.Path("Card.cosmos"), "Frame[class=card] > Text"},
                                    &Folder);
        ASSERT_TRUE(Unrooted.Out.find("color: var(--brand);  = #000000") != std::string::npos);
        const Run TooFar = RunCli({"explain", "--root", "3", Folder.Path("Card.cosmos"), "Frame > Text"}, &Folder);
        ASSERT_EQUAL(TooFar.Exit, kExitUsage);
        ASSERT_TRUE(TooFar.Err.starts_with("error: '--root 3' is past the path's 2 elements\n"));
        const Run BadPath = RunCli({"explain", Folder.Path("Card.cosmos"), "Frame >"}, &Folder);
        ASSERT_EQUAL(BadPath.Exit, kExitUsage);
        ASSERT_TRUE(BadPath.Err.starts_with("error: element path, column 8: expected an element\n"));
    });
});
