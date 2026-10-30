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
    return IsAsciiDigit(c)
        ? static_cast<u32>(c - '0')
        : static_cast<u32>((c >= 'a' && c <= 'f') ? (c - 'a' + 10) : (c - 'A' + 10));
}

[[nodiscard]] constexpr b8 IsBinaryDigit(const MChar c)noexcept{
    return c == '0' || c == '1';
}

[[nodiscard]] constexpr b8 TryDecodeCharLiteral(const MStringView text, i64& outValue)noexcept{
    if(text.size() < 3u || text.front() != '\'' || text.back() != '\'')
        return false;
    if(text[1] != '\\'){
        outValue = static_cast<i64>(static_cast<u8>(text[1]));
        return true;
    }
    if(text.size() < 4u)
        return false;

    switch(text[2]){
    case 'n': outValue = '\n'; return true;
    case 'r': outValue = '\r'; return true;
    case 't': outValue = '\t'; return true;
    case '\\': outValue = '\\'; return true;
    case '\'': outValue = '\''; return true;
    case '"': outValue = '"'; return true;
    case 'a': outValue = '\a'; return true;
    case 'b': outValue = '\b'; return true;
    case 'f': outValue = '\f'; return true;
    case 'v': outValue = '\v'; return true;
    case 'x':
    case 'X':
    case 'u':
    case 'U':{
        u64 val = 0u;
        for(usize i = 3u; i + 1u < text.size(); ++i){
            const MChar ch = text[i];
            if(!IsHexDigit(ch))
                return false;
            val = (val << 4u) | static_cast<u64>(HexDigitValue(ch));
        }
        outValue = static_cast<i64>(val);
        return true;
    }
    default:
        if(text[2] >= '0' && text[2] <= '7'){
            u64 val = 0u;
            for(usize i = 2u; i + 1u < text.size() && text[i] >= '0' && text[i] <= '7'; ++i)
                val = (val << 3u) | static_cast<u64>(text[i] - '0');
            outValue = static_cast<i64>(val);
            return true;
        }
        outValue = static_cast<i64>(static_cast<u8>(text[2]));
        return true;
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
        for(usize i = 2u; i < text.size(); ++i){
            const MChar ch = text[i];
            if(ch == '\'')
                continue;
            if(!IsHexDigit(ch))
                return false;
            val = (val << 4u) | static_cast<u64>(HexDigitValue(ch));
        }
        outValue = val;
        return true;
    }

    if(text.size() > 2u && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')){
        u64 val = 0u;
        for(usize i = 2u; i < text.size(); ++i){
            const MChar ch = text[i];
            if(ch == '\'')
                continue;
            if(!IsBinaryDigit(ch))
                return false;
            val = (val << 1u) | static_cast<u64>(ch - '0');
        }
        outValue = val;
        return true;
    }

    u64 val = 0u;
    for(usize i = 0u; i < text.size(); ++i){
        const MChar ch = text[i];
        if(ch == '\'')
            continue;
        if(!IsAsciiDigit(ch))
            return false;
        const u64 digit = static_cast<u64>(ch - '0');
        if(val > (Limit<u64>::s_Max - digit) / 10u)
            return false;
        val = val * 10u + digit;
    }
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
    [[nodiscard]] Token readCharLiteral()noexcept;
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
    u32 m_savedLines[64] = {};
    u32 m_savedLineDepth = 0;
    b8 m_hasPendingError = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

