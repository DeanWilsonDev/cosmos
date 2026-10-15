#include "Cosmos/Tokenizer.h"

#include <cctype>

namespace Cosmos {

namespace {
bool IsIdentStart(char C) { return std::isalpha(static_cast<unsigned char>(C)) || C == '_'; }
bool IsIdentChar(char C) { return std::isalnum(static_cast<unsigned char>(C)) || C == '_' || C == '-'; }
bool IsDigit(char C) { return std::isdigit(static_cast<unsigned char>(C)) != 0; }
bool StartsUnsignedNumber(char C, char Next) { return IsDigit(C) || (C == '.' && IsDigit(Next)); }
bool StartsNumber(char C, char Next, char AfterNext) {
    return StartsUnsignedNumber(C, Next) || (C == '-' && StartsUnsignedNumber(Next, AfterNext));
}
} // namespace

Tokenizer::Tokenizer(std::string_view Source, std::string FilePath)
    : Source_(Source), FilePath_(std::move(FilePath)) {}

bool Tokenizer::AtEnd() const { return Pos_ >= Source_.size(); }

char Tokenizer::CurrentChar() const { return AtEnd() ? '\0' : Source_[Pos_]; }

char Tokenizer::PeekChar(std::size_t Offset) const {
    const std::size_t Index = Pos_ + Offset;
    return Index < Source_.size() ? Source_[Index] : '\0';
}

void Tokenizer::Advance() {
    if (AtEnd()) {
        return;
    }
    if (Source_[Pos_] == '\n') {
        ++Line_;
        Column_ = 1;
    } else {
        ++Column_;
    }
    ++Pos_;
}

SourceLocation Tokenizer::CurrentLocation() const { return SourceLocation{FilePath_, Line_, Column_}; }

void Tokenizer::SkipWhitespaceAndComments() {
    for (;;) {
        while (!AtEnd() && std::isspace(static_cast<unsigned char>(CurrentChar()))) {
            Advance();
        }
        if (CurrentChar() == '/' && PeekChar() == '*') {
            Advance(); // /
            Advance(); // *
            while (!AtEnd() && !(CurrentChar() == '*' && PeekChar() == '/')) {
                Advance();
            }
            if (!AtEnd()) {
                Advance(); // *
                Advance(); // /
            }
            continue;
        }
        break;
    }
}

Token Tokenizer::NextToken() {
    SkipWhitespaceAndComments();

    if (AtEnd()) {
        return Token{TokenKind::EndOfFile, "", CurrentLocation()};
    }

    const char C = CurrentChar();

    if (C == '.' && IsIdentStart(PeekChar())) {
        return LexClassName();
    }
    if (C == ':' && IsIdentStart(PeekChar())) {
        return LexPseudoClass();
    }
    if (C == '@' && IsIdentStart(PeekChar())) {
        return LexAtKeyword();
    }
    if (C == '-' && PeekChar() == '-' && IsIdentStart(PeekChar(2))) {
        return LexVariableName();
    }
    if (IsIdentStart(C)) {
        return LexIdentifierLike();
    }
    if (StartsNumber(C, PeekChar(), PeekChar(2))) {
        return LexNumber();
    }
    if (C == '"') {
        return LexStringLiteral();
    }
    if (C == '#') {
        return LexHexColor();
    }

    const SourceLocation Loc = CurrentLocation();
    switch (C) {
    case '{': Advance(); return Token{TokenKind::OpenBrace, "{", Loc};
    case '}': Advance(); return Token{TokenKind::CloseBrace, "}", Loc};
    case '(': Advance(); return Token{TokenKind::OpenParen, "(", Loc};
    case ')': Advance(); return Token{TokenKind::CloseParen, ")", Loc};
    case ':': Advance(); return Token{TokenKind::Colon, ":", Loc};
    case ';': Advance(); return Token{TokenKind::Semicolon, ";", Loc};
    case ',': Advance(); return Token{TokenKind::Comma, ",", Loc};
    default:
        Advance();
        return Token{TokenKind::Invalid, std::string(1, C), Loc};
    }
}

Token Tokenizer::LexIdentifierLike() {
    const SourceLocation Loc = CurrentLocation();
    const std::size_t    Start = Pos_;
    while (!AtEnd() && IsIdentChar(CurrentChar())) {
        Advance();
    }
    std::string Text(Source_.substr(Start, Pos_ - Start));
    if (Text == "var") {
        return Token{TokenKind::VarKeyword, std::move(Text), Loc};
    }
    if (Text == "root") {
        return Token{TokenKind::RootKeyword, std::move(Text), Loc};
    }
    return Token{TokenKind::Identifier, std::move(Text), Loc};
}

Token Tokenizer::LexClassName() {
    const SourceLocation Loc = CurrentLocation();
    Advance(); // .
    const std::size_t Start = Pos_;
    while (!AtEnd() && IsIdentChar(CurrentChar())) {
        Advance();
    }
    return Token{TokenKind::ClassName, std::string(Source_.substr(Start, Pos_ - Start)), Loc};
}

Token Tokenizer::LexAtKeyword() {
    const SourceLocation Loc = CurrentLocation();
    Advance(); // @
    const std::size_t Start = Pos_;
    while (!AtEnd() && IsIdentChar(CurrentChar())) {
        Advance();
    }
    return Token{TokenKind::AtKeyword, std::string(Source_.substr(Start, Pos_ - Start)), Loc};
}

Token Tokenizer::LexPseudoClass() {
    const SourceLocation Loc = CurrentLocation();
    Advance(); // :
    const std::size_t Start = Pos_;
    while (!AtEnd() && IsIdentChar(CurrentChar())) {
        Advance();
    }
    return Token{TokenKind::PseudoClass, std::string(Source_.substr(Start, Pos_ - Start)), Loc};
}

Token Tokenizer::LexVariableName() {
    const SourceLocation Loc = CurrentLocation();
    Advance(); // -
    Advance(); // -
    const std::size_t Start = Pos_;
    while (!AtEnd() && IsIdentChar(CurrentChar())) {
        Advance();
    }
    return Token{TokenKind::VariableName, std::string(Source_.substr(Start, Pos_ - Start)), Loc};
}

Token Tokenizer::LexNumber() {
    const SourceLocation Loc = CurrentLocation();
    const std::size_t    Start = Pos_;
    if (CurrentChar() == '-') {
        Advance();
    }
    while (!AtEnd() && std::isdigit(static_cast<unsigned char>(CurrentChar()))) {
        Advance();
    }
    if (CurrentChar() == '.' && std::isdigit(static_cast<unsigned char>(PeekChar()))) {
        Advance();
        while (!AtEnd() && std::isdigit(static_cast<unsigned char>(CurrentChar()))) {
            Advance();
        }
    }
    while (!AtEnd() && (std::isalpha(static_cast<unsigned char>(CurrentChar())) || CurrentChar() == '%')) {
        Advance();
    }
    return Token{TokenKind::Number, std::string(Source_.substr(Start, Pos_ - Start)), Loc};
}

Token Tokenizer::LexStringLiteral() {
    const SourceLocation Loc = CurrentLocation();
    Advance(); // opening "
    const std::size_t Start = Pos_;
    while (!AtEnd() && CurrentChar() != '"') {
        Advance();
    }
    const std::string Text(Source_.substr(Start, Pos_ - Start));
    if (!AtEnd()) {
        Advance(); // closing "
    }
    return Token{TokenKind::StringLiteral, Text, Loc};
}

Token Tokenizer::LexHexColor() {
    const SourceLocation Loc = CurrentLocation();
    const std::size_t    Start = Pos_;
    Advance(); // #
    while (!AtEnd() && std::isxdigit(static_cast<unsigned char>(CurrentChar()))) {
        Advance();
    }
    return Token{TokenKind::HexColor, std::string(Source_.substr(Start, Pos_ - Start)), Loc};
}

} // namespace Cosmos
