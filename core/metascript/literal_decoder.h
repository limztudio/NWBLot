// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class LiteralDecoder final{
public:
    [[nodiscard]] static constexpr b8 IsIntegerLiteralSuffixChar(const MChar c)noexcept{
        return c == 'u' || c == 'U' || c == 'l' || c == 'L';
    }

    [[nodiscard]] static constexpr b8 TryDecodeSimpleEscapeChar(const MChar esc, MChar* outDecoded = nullptr)noexcept{
        struct SimpleEscapePair{
            MChar escapeChar;
            MChar decodedChar;
        };
        constexpr Array<SimpleEscapePair, 11> s_SimpleEscapes = {{
            { 'n',  '\n' },
            { 'r',  '\r' },
            { 't',  '\t' },
            { '\\', '\\' },
            { '\'', '\'' },
            { '"',  '"'  },
            { '?',  '?'  },
            { 'a',  '\a' },
            { 'b',  '\b' },
            { 'f',  '\f' },
            { 'v',  '\v' },
        }};
        for(const SimpleEscapePair& pair : s_SimpleEscapes){
            if(pair.escapeChar == esc){
                if(outDecoded)
                    *outDecoded = pair.decodedChar;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static constexpr b8 TryDecodeCharLiteral(const MStringView text, i64& outValue)noexcept{
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

        MChar simpleDecoded = '\0';
        if(TryDecodeSimpleEscapeChar(text[2], &simpleDecoded)){
            if(text.size() != 4u)
                return false;
            outValue = static_cast<i64>(static_cast<u8>(simpleDecoded));
            return true;
        }

        switch(text[2]){
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
            if((text[2] == 'u' || text[2] == 'U') && !(val <= s_UnicodeMaxCodePoint && IsUnicodeScalarValue(static_cast<u32>(val))))
                return false;
            outValue = static_cast<i64>(val);
            return true;
        }
        default:
            if(IsOctalDigit(text[2])){
                u64 val = 0u;
                for(usize i = 2u; i + 1u < text.size(); ++i){
                    if(!IsOctalDigit(text[i]) || val > (Limit<u64>::s_Max >> 3u))
                        return false;
                    val = (val << 3u) | static_cast<u64>(text[i] - '0');
                }
                outValue = static_cast<i64>(val);
                return true;
            }
            return false;
        }
    }

    [[nodiscard]] static constexpr b8 TryParseUnsignedIntegerLiteral(MStringView text, u64& outValue)noexcept{
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

        while(!text.empty() && IsIntegerLiteralSuffixChar(text.back()))
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
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_METASCRIPT_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

