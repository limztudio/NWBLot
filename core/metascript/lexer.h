// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr MStringView s_IncludePushMarker = "/*__adhd_inc_push__*/";
inline constexpr MStringView s_IncludePopMarker = "/*__adhd_inc_pop__*/";

[[nodiscard]] constexpr b8 IsHexDigit(const MChar c)noexcept{
    return IsAsciiDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

[[nodiscard]] constexpr u32 HexDigitValue(const MChar c)noexcept{
    return IsAsciiDigit(c) ? static_cast<u32>(c - '0') : static_cast<u32>((c >= 'a' && c <= 'f') ? (c - 'a' + 10) : (c - 'A' + 10));
}

[[nodiscard]] constexpr b8 IsBinaryDigit(const MChar c)noexcept{
    return c == '0' || c == '1';
}

[[nodiscard]] constexpr b8 TryDecodeCharLiteral(const MStringView text, i64& outValue)noexcept{
    if(text.size() < 3u || text.front() != '\'' || text.back() != '\'')
        return false;
    if(text[1] != '\\'){
        if(text.size() != 3u)
            return false;
        outValue = static_cast<i64>(static_cast<u8>(text[1]));
        return true;
    }
    if(text.size() < 4u)
        return false;

    switch(text[2]){
    case 'n':
        if(text.size() != 4u)
            return false;
        outValue = '\n';
        return true;
    case 'r':
        if(text.size() != 4u)
            return false;
        outValue = '\r';
        return true;
    case 't':
        if(text.size() != 4u)
            return false;
        outValue = '\t';
        return true;
    case '\\':
        if(text.size() != 4u)
            return false;
        outValue = '\\';
        return true;
    case '\'':
        if(text.size() != 4u)
            return false;
        outValue = '\'';
        return true;
    case '"':
        if(text.size() != 4u)
            return false;
        outValue = '"';
        return true;
    case '?':
        if(text.size() != 4u)
            return false;
        outValue = '?';
        return true;
    case 'a':
        if(text.size() != 4u)
            return false;
        outValue = '\a';
        return true;
    case 'b':
        if(text.size() != 4u)
            return false;
        outValue = '\b';
        return true;
    case 'f':
        if(text.size() != 4u)
            return false;
        outValue = '\f';
        return true;
    case 'v':
        if(text.size() != 4u)
            return false;
        outValue = '\v';
        return true;
    case 'x':
    case 'X':
    case 'u':
    case 'U':{
        if(text.size() <= 4u)
            return false;
        u64 val = 0u;
        for(usize i = 3u; i + 1u < text.size(); ++i){
            const MChar ch = text[i];
            if(!IsHexDigit(ch) || val > (Limit<u64>::s_Max >> 4u))
                return false;
            val = (val << 4u) | static_cast<u64>(HexDigitValue(ch));
        }
        if((text[2] == 'u' || text[2] == 'U') && (val > 0x10FFFFu || (val >= 0xD800u && val <= 0xDFFFu)))
            return false;
        outValue = static_cast<i64>(val);
        return true;
    }
    default:
        if(text[2] >= '0' && text[2] <= '7'){
            u64 val = 0u;
            for(usize i = 2u; i + 1u < text.size(); ++i){
                if(text[i] < '0' || text[i] > '7' || val > (Limit<u64>::s_Max >> 3u))
                    return false;
                val = (val << 3u) | static_cast<u64>(text[i] - '0');
            }
            outValue = static_cast<i64>(val);
            return true;
        }
        return false;
    }
}

[[nodiscard]] constexpr b8 TryParseUnsignedIntegerLiteral(MStringView text, u64& outValue)noexcept{
    text = TrimView(text);
    if(text.empty())
        return false;

    if(text.size() >= 3u && text.front() == '\'' && text.back() == '\''){
        i64 charVal = 0;
        if(!TryDecodeCharLiteral(text, charVal))
            return false;
        outValue = static_cast<u64>(charVal);
        return true;
    }

    while(!text.empty() && (text.back() == 'u' || text.back() == 'U' || text.back() == 'l' || text.back() == 'L'))
        text.remove_suffix(1u);
    if(text.empty())
        return false;

    if(text.size() > 2u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')){
        u64 val = 0u;
        b8 hasDigit = false;
        for(usize i = 2u; i < text.size(); ++i){
            const MChar ch = text[i];
            if(ch == '\'')
                continue;
            if(!IsHexDigit(ch) || val > (Limit<u64>::s_Max >> 4u))
                return false;
            hasDigit = true;
            val = (val << 4u) | static_cast<u64>(HexDigitValue(ch));
        }
        if(!hasDigit)
            return false;
        outValue = val;
        return true;
    }

    if(text.size() > 2u && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')){
        u64 val = 0u;
        b8 hasDigit = false;
        for(usize i = 2u; i < text.size(); ++i){
            const MChar ch = text[i];
            if(ch == '\'')
                continue;
            if(!IsBinaryDigit(ch) || val > (Limit<u64>::s_Max >> 1u))
                return false;
            hasDigit = true;
            val = (val << 1u) | static_cast<u64>(ch - '0');
        }
        if(!hasDigit)
            return false;
        outValue = val;
        return true;
    }

    u64 val = 0u;
    b8 hasDigit = false;
    for(usize i = 0u; i < text.size(); ++i){
        const MChar ch = text[i];
        if(ch == '\'')
            continue;
        if(!IsAsciiDigit(ch))
            return false;
        hasDigit = true;
        const u64 digit = static_cast<u64>(ch - '0');
        if(val > (Limit<u64>::s_Max - digit) / 10u)
            return false;
        val = val * 10u + digit;
    }
    if(!hasDigit)
        return false;
    outValue = val;
    return true;
}


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
    Array<u32, 64> m_savedLines{};
    u32 m_savedLineDepth = 0;
    b8 m_hasPendingError = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


