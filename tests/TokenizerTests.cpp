#include "cimmerian/test.hpp"

#include "Cosmos/Tokenizer.h"

using namespace Cosmos;

DESCRIBE("Tokenizer", {
    IT("lexes a class selector and a declaration", {
        Tokenizer Tok(".card { padding: 16px; }", "test.cosmos");

        const Token T1 = Tok.NextToken();
        ASSERT_TRUE(T1.Kind == TokenKind::ClassName && T1.Text == "card"); // class selector without leading dot

        const Token T2 = Tok.NextToken();
        ASSERT_TRUE(T2.Kind == TokenKind::OpenBrace);

        const Token T3 = Tok.NextToken();
        ASSERT_TRUE(T3.Kind == TokenKind::Identifier && T3.Text == "padding");

        const Token T4 = Tok.NextToken();
        ASSERT_TRUE(T4.Kind == TokenKind::Colon);

        const Token T5 = Tok.NextToken();
        ASSERT_TRUE(T5.Kind == TokenKind::Number && T5.Text == "16px"); // unit suffix stays attached

        const Token T6 = Tok.NextToken();
        ASSERT_TRUE(T6.Kind == TokenKind::Semicolon);

        const Token T7 = Tok.NextToken();
        ASSERT_TRUE(T7.Kind == TokenKind::CloseBrace);

        const Token T8 = Tok.NextToken();
        ASSERT_TRUE(T8.Kind == TokenKind::EndOfFile);
    });

    IT("lexes a leading minus as part of a number", {
        Tokenizer Tok("margin: -6px -.5px 0px; --gap: -4px", "test.cosmos");

        ASSERT_TRUE(Tok.NextToken().Kind == TokenKind::Identifier);
        ASSERT_TRUE(Tok.NextToken().Kind == TokenKind::Colon);

        const Token T3 = Tok.NextToken();
        ASSERT_TRUE(T3.Kind == TokenKind::Number && T3.Text == "-6px");

        const Token T4 = Tok.NextToken();
        ASSERT_TRUE(T4.Kind == TokenKind::Number && T4.Text == "-.5px");

        const Token T5 = Tok.NextToken();
        ASSERT_TRUE(T5.Kind == TokenKind::Number && T5.Text == "0px");

        ASSERT_TRUE(Tok.NextToken().Kind == TokenKind::Semicolon);

        const Token T7 = Tok.NextToken();
        ASSERT_TRUE(T7.Kind == TokenKind::VariableName && T7.Text == "gap");

        ASSERT_TRUE(Tok.NextToken().Kind == TokenKind::Colon);

        const Token T9 = Tok.NextToken();
        ASSERT_TRUE(T9.Kind == TokenKind::Number && T9.Text == "-4px");
    });

    IT("still lexes a lone minus as invalid", {
        Tokenizer Tok("- 6px", "test.cosmos");

        const Token T1 = Tok.NextToken();
        ASSERT_TRUE(T1.Kind == TokenKind::Invalid && T1.Text == "-");
    });

    IT("lexes variable declarations and var() references", {
        Tokenizer Tok("--color-primary: #E8593C; var(--color-primary)", "test.cosmos");

        const Token T1 = Tok.NextToken();
        ASSERT_TRUE(T1.Kind == TokenKind::VariableName && T1.Text == "color-primary");

        Tok.NextToken(); // colon
        const Token T3 = Tok.NextToken();
        ASSERT_TRUE(T3.Kind == TokenKind::HexColor && T3.Text == "#E8593C");

        Tok.NextToken(); // semicolon
        const Token T5 = Tok.NextToken();
        ASSERT_TRUE(T5.Kind == TokenKind::VarKeyword);

        Tok.NextToken(); // open paren
        const Token T7 = Tok.NextToken();
        ASSERT_TRUE(T7.Kind == TokenKind::VariableName && T7.Text == "color-primary");
    });

    IT("lexes pseudo-classes and :root", {
        Tokenizer Tok(":hover :root", "test.cosmos");

        const Token T1 = Tok.NextToken();
        ASSERT_TRUE(T1.Kind == TokenKind::PseudoClass && T1.Text == "hover");

        const Token T2 = Tok.NextToken();
        ASSERT_TRUE(T2.Kind == TokenKind::PseudoClass && T2.Text == "root");
        // `:root` lexes as a pseudo-class-shaped token; the parser gives it
        // special meaning at the top level (docs/cosmos_core_spec.md §1.4).
    });

    IT("skips block comments but not their content", {
        Tokenizer Tok("/* a comment { with braces } */ .card { }", "test.cosmos");

        const Token T1 = Tok.NextToken();
        ASSERT_TRUE(T1.Kind == TokenKind::ClassName && T1.Text == "card"); // comment fully skipped
    });

    IT("lexes string literals", {
        Tokenizer Tok("\"assets/fonts/body.ttf\"", "test.cosmos");

        const Token T1 = Tok.NextToken();
        ASSERT_TRUE(T1.Kind == TokenKind::StringLiteral && T1.Text == "assets/fonts/body.ttf");
    });
});
