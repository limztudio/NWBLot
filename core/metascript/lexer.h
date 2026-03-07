// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TokenType{
    enum Enum : u8{
        Identifier = 0,
        IntegerLiteral,
        DoubleLiteral,
        StringLiteral,

        Plus,
        Minus,
        Star,
        Slash,
        PlusEqual,
        MinusEqual,
        StarEqual,
        SlashEqual,
        Equal,

        Semicolon,
        Dot,
        Comma,
        Colon,
        LeftBracket,
        RightBracket,
        LeftBrace,
        RightBrace,
        LeftParen,
        RightParen,

        EndOfFile,
        Error,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Token{
    TokenType::Enum type = TokenType::EndOfFile;
    AStringView text;
    u32 line = 1;
    u32 column = 1;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Lexer{
public:
    Lexer(AStringView source);


public:
    [[nodiscard]] Token next();
    [[nodiscard]] u32 currentLine()const{ return m_line; }
    [[nodiscard]] u32 currentColumn()const{ return m_column; }


private:
    void skipWhitespaceAndComments();
    [[nodiscard]] Token readIdentifier();
    [[nodiscard]] Token readNumber();
    [[nodiscard]] Token readString();
    [[nodiscard]] Token makeToken(TokenType::Enum type, usize length);
    [[nodiscard]] Token makeErrorToken(AStringView message);

    [[nodiscard]] char peek()const;
    [[nodiscard]] char peekNext()const;
    char advance();
    [[nodiscard]] bool isAtEnd()const{ return m_current >= m_source.size(); }


private:
    AStringView m_source;
    usize m_current = 0;
    u32 m_line = 1;
    u32 m_column = 1;
    bool m_hasPendingError = false;
    u32 m_errorLine = 1;
    u32 m_errorColumn = 1;
    AStringView m_errorMessage;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

