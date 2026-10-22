// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace JsonTextDetail{
inline constexpr u8 s_ControlMaxExclusive = 0x20u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename StringT>
inline void AppendJsonEscapedText(StringT& out, const AStringView text)noexcept(
    noexcept(out += DeclVal<const char(&)[3]>()) && noexcept(out += '?') && noexcept(out += DeclVal<const char&>())
){
    for(const char ch : text){
        switch(ch){
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if(static_cast<unsigned char>(ch) < JsonTextDetail::s_ControlMaxExclusive)
                out += '?';
            else
                out += ch;
            break;
        }
    }
}

template<typename StringT>
inline void AppendJsonQuotedText(StringT& out, const AStringView text)noexcept(noexcept(out += '"') && noexcept(AppendJsonEscapedText(out, text))){
    out += '"';
    AppendJsonEscapedText(out, text);
    out += '"';
}

template<typename StringT>
[[nodiscard]] inline StringT MakeJsonEscapedText(const AStringView text)noexcept(
    noexcept(StringT{}) && noexcept(DeclVal<StringT&>().reserve(text.size())) && noexcept(AppendJsonEscapedText(DeclVal<StringT&>(), text))
    && IsNothrowMoveConstructible_V<StringT> && IsNothrowDestructible_V<StringT>
){
    StringT out;
    out.reserve(text.size());
    AppendJsonEscapedText(out, text);
    return out;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

