#include "Cosmos/Formatter.h"

#include "Cosmos/Tokenizer.h"

#include <algorithm>

namespace Cosmos {

namespace {

std::vector<Token> CommentedTokens(std::string_view Source, const std::string& FilePath) {
    Tokenizer          Lexer(Source, FilePath, CommentHandling::Emit);
    std::vector<Token> Tokens;
    while (true) {
        Token Next = Lexer.NextToken();
        if (Next.Kind == TokenKind::EndOfFile) {
            break;
        }
        Tokens.push_back(std::move(Next));
    }
    return Tokens;
}

std::vector<std::string> CommentsOf(const std::vector<Token>& Tokens) {
    std::vector<std::string> Comments;
    for (const Token& T : Tokens) {
        if (T.Kind == TokenKind::Comment) {
            Comments.push_back(T.Text);
        }
    }
    return Comments;
}

class Printer {
public:
    Printer(std::string_view Source, std::vector<Token> Tokens, const FormatOptions& Options)
        : Source_(Source), Tokens_(std::move(Tokens)), Options_(Options) {
        LineStarts_.push_back(0);
        for (std::size_t I = 0; I < Source_.size(); ++I) {
            if (Source_[I] == '\n') {
                LineStarts_.push_back(I + 1);
            }
        }
    }

    std::string Print() {
        PrintItems(0, 0);
        std::string Text;
        for (const std::string& Line : Lines_) {
            Text += Line;
            Text += '\n';
        }
        return Text;
    }

private:
    struct Piece {
        std::string Text;
        TokenKind   Kind;
    };

    std::size_t Offset(const SourceLocation& Location) const {
        if (Location.Line == 0 || Location.Line > LineStarts_.size()) {
            return Source_.size();
        }
        return std::min(LineStarts_[Location.Line - 1] + Location.Column - 1, Source_.size());
    }

    std::string Written(const Token& T) const {
        const std::size_t Start = Offset(T.Location);
        const std::size_t End   = Offset(T.End);
        return std::string(Source_.substr(Start, End > Start ? End - Start : 0));
    }

    std::string Indent(int Level) const { return std::string(static_cast<std::size_t>(Level * Options_.IndentWidth), ' '); }

    void Emit(int Level, const std::string& Text) { Lines_.push_back(Indent(Level) + Text); }

    bool At(std::size_t I, TokenKind Kind) const { return I < Tokens_.size() && Tokens_[I].Kind == Kind; }

    // The first `{`, `;` or `}` at or after I outside parentheses, or the end.
    std::size_t Terminator(std::size_t I) const {
        int Depth = 0;
        for (; I < Tokens_.size(); ++I) {
            const TokenKind Kind = Tokens_[I].Kind;
            if (Kind == TokenKind::OpenParen) {
                ++Depth;
            } else if (Kind == TokenKind::CloseParen) {
                Depth = std::max(Depth - 1, 0);
            } else if (Depth == 0 &&
                       (Kind == TokenKind::OpenBrace || Kind == TokenKind::Semicolon || Kind == TokenKind::CloseBrace)) {
                return I;
            }
        }
        return Tokens_.size();
    }

    std::size_t PrintItems(std::size_t I, int Level) {
        bool First        = true;
        bool AfterBlock   = false;
        while (I < Tokens_.size() && Tokens_[I].Kind != TokenKind::CloseBrace) {
            const Token& T = Tokens_[I];
            if (T.Kind == TokenKind::Comment && !Lines_.empty() && T.Location.Line == LastLine_) {
                Lines_.back() += " " + Written(T);
                LastLine_ = T.End.Line;
                ++I;
                continue;
            }
            const bool AuthorBlank = !First && T.Location.Line > LastLine_ + 1;
            if (!First && (AuthorBlank || (Level == 0 && AfterBlock))) {
                Lines_.emplace_back();
            }
            First = false;
            if (T.Kind == TokenKind::Comment) {
                Emit(Level, Written(T));
                LastLine_  = T.End.Line;
                AfterBlock = false;
                ++I;
                continue;
            }
            const std::size_t End = Terminator(I);
            if (At(End, TokenKind::OpenBrace)) {
                I          = PrintBlock(I, End, Level);
                AfterBlock = true;
            } else {
                I          = PrintDeclaration(I, End, Level);
                AfterBlock = false;
            }
        }
        return I;
    }

    std::string Joined(std::size_t From, std::size_t To) const {
        std::string Text;
        for (std::size_t I = From; I < To; ++I) {
            if (I > From && Offset(Tokens_[I].Location) > Offset(Tokens_[I - 1].End)) {
                Text += ' ';
            }
            Text += Written(Tokens_[I]);
        }
        return Text;
    }

    std::size_t PrintBlock(std::size_t HeadStart, std::size_t OpenBrace, int Level) {
        const std::string Head = Joined(HeadStart, OpenBrace);
        if (At(OpenBrace + 1, TokenKind::CloseBrace)) {
            Emit(Level, Head + " {}");
            LastLine_ = Tokens_[OpenBrace + 1].End.Line;
            return OpenBrace + 2;
        }
        Emit(Level, Head + " {");
        LastLine_            = Tokens_[OpenBrace].End.Line;
        const std::size_t I = PrintItems(OpenBrace + 1, Level + 1);
        Emit(Level, "}");
        if (I < Tokens_.size()) {
            LastLine_ = Tokens_[I].End.Line;
            return I + 1;
        }
        return I;
    }

    static std::string JoinPieces(const std::vector<Piece>& Pieces) {
        std::string Text;
        for (std::size_t I = 0; I < Pieces.size(); ++I) {
            const TokenKind Kind = Pieces[I].Kind;
            const bool      Tight =
                I == 0 || Kind == TokenKind::Comma || Kind == TokenKind::CloseParen || Kind == TokenKind::OpenParen ||
                Pieces[I - 1].Kind == TokenKind::OpenParen;
            if (!Tight) {
                Text += ' ';
            }
            Text += Pieces[I].Text;
        }
        return Text;
    }

    std::size_t PrintDeclaration(std::size_t Start, std::size_t End, int Level) {
        std::string        Name = Written(Tokens_[Start]);
        std::vector<Piece> Values;
        std::size_t        I = Start + 1;
        if (At(I, TokenKind::Colon)) {
            ++I;
        }
        for (; I < End; ++I) {
            Values.push_back(Piece{Written(Tokens_[I]), Tokens_[I].Kind});
        }

        std::vector<std::vector<Piece>> Groups(1);
        int                             Depth = 0;
        for (const Piece& Each : Values) {
            if (Each.Kind == TokenKind::OpenParen) {
                ++Depth;
            } else if (Each.Kind == TokenKind::CloseParen) {
                Depth = std::max(Depth - 1, 0);
            }
            if (Each.Kind == TokenKind::Comma && Depth == 0) {
                Groups.emplace_back();
                continue;
            }
            Groups.back().push_back(Each);
        }

        const std::string OneLine = Name + ": " + JoinPieces(Values) + ";";
        const bool        TooLong = static_cast<int>(Indent(Level).size() + OneLine.size()) > Options_.LineWidth;
        if (!TooLong || Groups.size() == 1) {
            Emit(Level, OneLine);
        } else {
            Emit(Level, Name + ":");
            for (std::size_t G = 0; G < Groups.size(); ++G) {
                Emit(Level + 1, JoinPieces(Groups[G]) + (G + 1 == Groups.size() ? ";" : ","));
            }
        }

        const std::size_t Last = End > Start ? End - 1 : Start;
        LastLine_              = Tokens_[Last].End.Line;
        if (At(End, TokenKind::Semicolon)) {
            LastLine_ = Tokens_[End].End.Line;
            return End + 1;
        }
        return End;
    }

    std::string_view         Source_;
    std::vector<Token>       Tokens_;
    FormatOptions            Options_;
    std::vector<std::size_t> LineStarts_;
    std::vector<std::string> Lines_;
    std::uint32_t            LastLine_{0};
};

bool SameValue(const ValuePart& A, const ValuePart& B) {
    return A.Literal == B.Literal && A.Number == B.Number && A.StringValue == B.StringValue &&
           A.HexColor == B.HexColor && A.VariableRef == B.VariableRef && A.AttrRef == B.AttrRef &&
           A.ColorChannels == B.ColorChannels && A.CallArgument == B.CallArgument && A.FollowsComma == B.FollowsComma;
}

bool SameValues(const std::vector<ValuePart>& A, const std::vector<ValuePart>& B) {
    return std::ranges::equal(A, B, SameValue);
}

bool SameDeclarations(const std::vector<Declaration>& A, const std::vector<Declaration>& B) {
    return std::ranges::equal(A, B, [](const Declaration& X, const Declaration& Y) {
        return X.Property == Y.Property && SameValues(X.Values, Y.Values);
    });
}

bool SameVariables(const std::vector<VariableDeclaration>& A, const std::vector<VariableDeclaration>& B) {
    return std::ranges::equal(A, B, [](const VariableDeclaration& X, const VariableDeclaration& Y) {
        return X.Name == Y.Name && SameValue(X.Value, Y.Value);
    });
}

bool SameRules(const std::vector<RulePtr>& A, const std::vector<RulePtr>& B);

bool SameRule(const Rule& A, const Rule& B) {
    return A.Kind == B.Kind && A.ClassName == B.ClassName && A.PrimitiveName == B.PrimitiveName &&
           A.Pseudo == B.Pseudo && A.Depth == B.Depth && A.SourceIndex == B.SourceIndex &&
           SameDeclarations(A.Declarations, B.Declarations) &&
           SameDeclarations(A.StartingDeclarations, B.StartingDeclarations) && SameVariables(A.Variables, B.Variables) &&
           SameRules(A.NestedRules, B.NestedRules);
}

bool SameRules(const std::vector<RulePtr>& A, const std::vector<RulePtr>& B) {
    return std::ranges::equal(A, B, [](const RulePtr& X, const RulePtr& Y) { return SameRule(*X, *Y); });
}

bool SameSheet(const Stylesheet& A, const Stylesheet& B) {
    if (A.Root.has_value() != B.Root.has_value()) {
        return false;
    }
    if (A.Root && !SameVariables(A.Root->Variables, B.Root->Variables)) {
        return false;
    }
    return SameRules(A.Rules, B.Rules);
}

} // namespace

FormatResult FormatSource(std::string_view Source, const std::string& FilePath, const FormatOptions& Options) {
    FormatResult Result;
    Parser       Original(Source, FilePath);
    ParseResult  Parsed = Original.Parse();
    if (!Parsed.Errors.empty() || !Parsed.Sheet) {
        Result.Outcome = FormatOutcome::SyntaxErrors;
        Result.Errors  = std::move(Parsed.Errors);
        return Result;
    }

    std::vector<Token> Tokens   = CommentedTokens(Source, FilePath);
    const auto         Comments = CommentsOf(Tokens);
    Result.Formatted            = Printer(Source, std::move(Tokens), Options).Print();

    Parser            Reparser(Result.Formatted, FilePath);
    const ParseResult Reparsed = Reparser.Parse();
    if (!Reparsed.Errors.empty() || !Reparsed.Sheet) {
        Result.Outcome = FormatOutcome::Unverified;
        Result.Problem = "the output doesn't parse" +
                         (Reparsed.Errors.empty() ? std::string() : ": " + Reparsed.Errors.front().Message);
        return Result;
    }
    if (!SameSheet(*Parsed.Sheet, *Reparsed.Sheet)) {
        Result.Outcome = FormatOutcome::Unverified;
        Result.Problem = "the output's syntax tree differs from the input's";
        return Result;
    }
    if (CommentsOf(CommentedTokens(Result.Formatted, FilePath)) != Comments) {
        Result.Outcome = FormatOutcome::Unverified;
        Result.Problem = "the output's comments differ from the input's";
        return Result;
    }
    return Result;
}

} // namespace Cosmos
