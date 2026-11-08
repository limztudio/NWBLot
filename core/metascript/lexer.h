// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr MStringView s_IncludePushMarker = "/*__adhd_inc_push__*/";
inline constexpr MStringView s_IncludePopMarker = "/*__adhd_inc_pop__*/";
inline constexpr usize s_MaxSavedIncludeDepth = 64u;


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


class Lexer final{
public:
    explicit Lexer(const MStringView source)noexcept
        : m_source(source)
    {}


public:
    [[nodiscard]] Token next()noexcept;
    [[nodiscard]] b8 consumeBalancedBracesRaw(MStringView& outInnerCode)noexcept;


private:
    void skipWhitespaceAndComments()noexcept;
    [[nodiscard]] Token readIdentifier()noexcept;
    [[nodiscard]] Token readNumber()noexcept;
    [[nodiscard]] Token readString()noexcept;
    [[nodiscard]] Token readCharLiteral()noexcept;
    [[nodiscard]] Token makeToken(TokenType::Enum type, usize length)noexcept;
    [[nodiscard]] Token makeErrorToken(MStringView message)noexcept{ return makeErrorToken(message, m_line, m_column); }
    [[nodiscard]] Token makeErrorToken(MStringView message, u32 line, u32 column)noexcept;

    [[nodiscard]] MChar peek()const noexcept{ return peekAt(0u); }
    [[nodiscard]] MChar peekNext()const noexcept{ return peekAt(1u); }
    [[nodiscard]] MChar peekAt(const usize offset)const noexcept{ return m_current + offset >= m_source.size() ? '\0' : m_source[m_current + offset]; }
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
    Array<u32, s_MaxSavedIncludeDepth> m_savedLines{};
    u32 m_savedLineDepth = 0;
    b8 m_hasPendingError = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

