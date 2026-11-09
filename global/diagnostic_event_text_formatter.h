// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "array.h"
#include "error_code.h"

#include <charconv>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_MaxEventTextBytes = 2048u;
inline constexpr StringView s_TrueText = "true";
inline constexpr StringView s_FalseText = "false";


class DiagnosticEventTextFormatter final{
private:
static inline constexpr usize s_NumberTextBufferBytes = 128u;
static inline constexpr wchar s_AsciiCharacterMax = 0x7F;
static inline constexpr StringView s_NullText = "(null)";
static inline constexpr StringView s_PointerHexPrefix = "0x";
static inline constexpr StringView s_UnprintableText = "<unprintable>";
static inline constexpr i32 s_PointerTextRadix = 16;

public:
template<typename CharT>
[[nodiscard]] static constexpr char ToEventAsciiChar(const CharT ch)noexcept{
    if constexpr(IsSame_V<CharT, char>)
        return ch;
    else
        return ch >= 0 && ch <= s_AsciiCharacterMax ? static_cast<char>(ch) : '?';
}

static void AppendEventChar(Array<char, s_MaxEventTextBytes>& outText, usize& outCursor, const char ch)noexcept{
    if(outCursor + 1u >= s_MaxEventTextBytes)
        return;

    outText[outCursor++] = ch;
    outText[outCursor] = 0;
}

template<typename CharT>
static void AppendEventText(Array<char, s_MaxEventTextBytes>& outText, usize& outCursor, const BasicStringView<CharT> text)noexcept{
    for(const CharT ch : text)
        AppendEventChar(outText, outCursor, ToEventAsciiChar(ch));
}

template<typename CharT>
[[nodiscard]] static b8 TryAppendEscapedBrace(const BasicStringView<CharT> text, usize& inoutIndex, Array<char, s_MaxEventTextBytes>& outText, usize& outCursor)noexcept(IsArithmetic_V<CharT>){
    const CharT ch = text[inoutIndex];
    if(ch == static_cast<CharT>('{') && inoutIndex + 1u < text.size() && text[inoutIndex + 1u] == static_cast<CharT>('{')){
        AppendEventChar(outText, outCursor, '{');
        ++inoutIndex;
        return true;
    }
    if(ch == static_cast<CharT>('}') && inoutIndex + 1u < text.size() && text[inoutIndex + 1u] == static_cast<CharT>('}')){
        AppendEventChar(outText, outCursor, '}');
        ++inoutIndex;
        return true;
    }
    return false;
}

template<typename CharT>
static void AppendFormatLiteral(Array<char, s_MaxEventTextBytes>& outText, usize& outCursor, const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    for(usize i = 0u; i < text.size(); ++i){
        if(TryAppendEscapedBrace(text, i, outText, outCursor))
            continue;
        AppendEventChar(outText, outCursor, ToEventAsciiChar(text[i]));
    }
}

template<typename T>
static void AppendEventNumber(Array<char, s_MaxEventTextBytes>& outText, usize& outCursor, const T value)noexcept(IsArithmetic_V<T>){
    Array<char, s_NumberTextBufferBytes> buffer = {};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if(result.ec == ErrorCondition{})
        AppendEventText(outText, outCursor, AStringView(buffer.data(), static_cast<usize>(result.ptr - buffer.data())));
}

static void AppendEventPointer(Array<char, s_MaxEventTextBytes>& outText, usize& outCursor, const void* const value)noexcept{
    if(!value){
        AppendEventText(outText, outCursor, AStringView(s_NullText));
        return;
    }

    Array<char, s_NumberTextBufferBytes> buffer = {};
    const usize address = reinterpret_cast<usize>(value);
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), address, s_PointerTextRadix);
    if(result.ec != ErrorCondition{})
        return;

    AppendEventText(outText, outCursor, AStringView(s_PointerHexPrefix));
    AppendEventText(outText, outCursor, AStringView(buffer.data(), static_cast<usize>(result.ptr - buffer.data())));
}

template<typename T>
static void AppendEventArgument(Array<char, s_MaxEventTextBytes>& outText, usize& outCursor, const T& value)noexcept{
    using RawT = RemoveCVRef_T<T>;
    if constexpr(IsSame_V<RawT, b8>){
        AppendEventText(outText, outCursor, value ? AStringView(s_TrueText) : AStringView(s_FalseText));
    }
    else if constexpr(IsSame_V<RawT, char>){
        AppendEventChar(outText, outCursor, value);
    }
    else if constexpr(IsSame_V<RawT, wchar>){
        AppendEventChar(outText, outCursor, ToEventAsciiChar(value));
    }
    else if constexpr(IsSame_V<RawT, Nullptr>){
        AppendEventText(outText, outCursor, AStringView(s_NullText));
    }
    else if constexpr(IsPointer_V<RawT> && IsSame_V<RemoveCV_T<RemovePointer_T<RawT>>, char>){
        AppendEventText(outText, outCursor, value ? AStringView(value) : AStringView(s_NullText));
    }
    else if constexpr(IsPointer_V<RawT> && IsSame_V<RemoveCV_T<RemovePointer_T<RawT>>, wchar>){
        AppendEventText(outText, outCursor, value ? WStringView(value) : WStringView());
    }
    else if constexpr(IsPointer_V<RawT>){
        AppendEventPointer(outText, outCursor, value);
    }
    else if constexpr(IsIntegral_V<RawT> || IsFloatingPoint_V<RawT>){
        AppendEventNumber(outText, outCursor, value);
    }
    else if constexpr(IsEnum_V<RawT>){
        AppendEventNumber(outText, outCursor, static_cast<UnderlyingType_T<RawT>>(value));
    }
    else if constexpr(requires{ { AStringView(value) }noexcept; }){
        AppendEventText(outText, outCursor, AStringView(value));
    }
    else if constexpr(requires{ { WStringView(value) }noexcept; }){
        AppendEventText(outText, outCursor, WStringView(value));
    }
    else if constexpr(requires{ { AStringView(value.logText()) }noexcept; }){
        AppendEventText(outText, outCursor, AStringView(value.logText()));
    }
    else if constexpr(requires{ { AStringView(value.resolvedText()) }noexcept; }){
        AppendEventText(outText, outCursor, AStringView(value.resolvedText()));
    }
    else if constexpr(requires{ { WStringView(value.resolvedText()) }noexcept; }){
        AppendEventText(outText, outCursor, WStringView(value.resolvedText()));
    }
    else if constexpr(requires{ { AStringView(value.view()) }noexcept; }){
        AppendEventText(outText, outCursor, AStringView(value.view()));
    }
    else if constexpr(requires{ { WStringView(value.view()) }noexcept; }){
        AppendEventText(outText, outCursor, WStringView(value.view()));
    }
    else if constexpr(requires{ { AppendEventArgument(outText, outCursor, value.c_str()) }noexcept; }){
        AppendEventArgument(outText, outCursor, value.c_str());
    }
    else if constexpr(requires{ value.value; }){
        AppendEventArgument(outText, outCursor, value.value);
    }
    else{
        AppendEventText(outText, outCursor, AStringView(s_UnprintableText));
    }
}

template<typename CharT>
[[nodiscard]] static usize FindReplacementEnd(const BasicStringView<CharT> text, const usize begin)noexcept(IsArithmetic_V<CharT>){
    for(usize i = begin; i < text.size(); ++i){
        if(text[i] == static_cast<CharT>('}'))
            return i;
    }

    return text.size();
}

template<typename CharT>
static void FormatEventTextArgs(Array<char, s_MaxEventTextBytes>& outText, usize& outCursor, const BasicStringView<CharT> fmt)noexcept(IsArithmetic_V<CharT>){
    AppendFormatLiteral(outText, outCursor, fmt);
}

template<typename CharT, typename Arg, typename... Args>
static void FormatEventTextArgs(Array<char, s_MaxEventTextBytes>& outText, usize& outCursor, const BasicStringView<CharT> fmt, Arg&& arg, Args&&... args)noexcept(IsArithmetic_V<CharT>){
    for(usize i = 0u; i < fmt.size(); ++i){
        if(TryAppendEscapedBrace(fmt, i, outText, outCursor))
            continue;
        const CharT ch = fmt[i];
        if(ch == static_cast<CharT>('{')){
            AppendEventArgument(outText, outCursor, Forward<Arg>(arg));
            const usize replacementEnd = FindReplacementEnd(fmt, i + 1u);
            if(replacementEnd >= fmt.size())
                return;

            FormatEventTextArgs(outText, outCursor, fmt.substr(replacementEnd + 1u), Forward<Args>(args)...);
            return;
        }

        AppendEventChar(outText, outCursor, ToEventAsciiChar(ch));
    }
}

template<typename CharT, typename... Args>
static void FormatEventText(Array<char, s_MaxEventTextBytes>& outText, const BasicStringView<CharT> fmt, Args&&... args)noexcept(IsArithmetic_V<CharT>){
    usize outCursor = 0u;
    outText[0] = 0;
    FormatEventTextArgs(outText, outCursor, fmt, Forward<Args>(args)...);
}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

