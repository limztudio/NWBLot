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
        FloatLiteral,
        StringLiteral,

        Plus,
        Minus,
        Star,
        Slash,
        Percent,
        PlusEqual,
        MinusEqual,
        StarEqual,
        SlashEqual,
        PercentEqual,
        PlusPlus,
        MinusMinus,
        Arrow,
        Equal,
        EqualEqual,
        BangEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
        Ampersand,
        AmpersandEqual,
        AmpersandAmpersand,
        Pipe,
        PipeEqual,
        PipePipe,
        Caret,
        CaretEqual,
        Tilde,
        Bang,
        Question,
        LessLess,
        LessLessEqual,
        GreaterGreater,
        GreaterGreaterEqual,

        Semicolon,
        Dot,
        Comma,
        Colon,
        ColonColon,
        Hash,
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
    TokenType::Enum type = TokenType::EndOfFile;
    u32 line = 1;
    u32 column = 1;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Lexer{
public:
    explicit Lexer(MStringView source)noexcept;


public:
    [[nodiscard]] Token next()noexcept;
    [[nodiscard]] b8 consumeBalancedBracesRaw(MStringView& outInnerCode)noexcept;


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
    [[nodiscard]] MChar peekAt(usize offset)const noexcept;
    MChar advance()noexcept;
    [[nodiscard]] b8 isAtEnd()const noexcept{ return m_current >= m_source.size(); }


private:
    MStringView m_source;
    usize m_current = 0;
    MStringView m_errorMessage;
    u32 m_line = 1;
    u32 m_column = 1;
    u32 m_errorLine = 1;
    u32 m_errorColumn = 1;
    b8 m_hasPendingError = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

