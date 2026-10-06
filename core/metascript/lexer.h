// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TokenType{
    static constexpr u8 s_TokenTypeIdentifierBase = 0;
    enum Enum : u8{
        Identifier = s_TokenTypeIdentifierBase,
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
    MStringView text;
    u32 line = 1;
    u32 column = 1;
    TokenType::Enum type = TokenType::EndOfFile;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Lexer{
public:
    Lexer(MStringView source)noexcept;


public:
    [[nodiscard]] Token next()noexcept;
    [[nodiscard]] u32 currentLine()const noexcept{ return m_line; }
    [[nodiscard]] u32 currentColumn()const noexcept{ return m_column; }


private:
    void skipWhitespaceAndComments()noexcept;
    [[nodiscard]] Token readIdentifier()noexcept;
    [[nodiscard]] Token readNumber()noexcept;
    [[nodiscard]] Token readString()noexcept;
    [[nodiscard]] Token makeToken(TokenType::Enum type, usize length)noexcept;
    [[nodiscard]] Token makeErrorToken(MStringView message)noexcept;
    [[nodiscard]] Token makeErrorToken(MStringView message, u32 line, u32 column)noexcept;

    [[nodiscard]] MChar peek()const noexcept;
    [[nodiscard]] MChar peekNext()const noexcept;
    MChar advance()noexcept;
    [[nodiscard]] bool isAtEnd()const noexcept{ return m_current >= m_source.size(); }


private:
    MStringView m_source;
    usize m_current = 0;
    MStringView m_errorMessage;
    u32 m_line = 1;
    u32 m_column = 1;
    u32 m_errorLine = 1;
    u32 m_errorColumn = 1;
    bool m_hasPendingError = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

