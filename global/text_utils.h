// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "expected.h"
#include "limit.h"

#include <cctype>
#include <charconv>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline bool IsAsciiSpace(const CharT ch)noexcept(IsArithmetic_V<CharT>){
    return ch == CharT(' ') || ch == CharT('\t') || ch == CharT('\n') || ch == CharT('\r') || ch == CharT('\f') || ch == CharT('\v');
}

template<typename CharT>
[[nodiscard]] inline BasicStringView<CharT> SafeStringView(const CharT* const text)noexcept{
    return text ? BasicStringView<CharT>(text) : BasicStringView<CharT>();
}

[[nodiscard]] inline constexpr StringView BoolToYesNoText(const bool value)noexcept{
    return value ? StringView("yes") : StringView("no");
}

[[nodiscard]] inline constexpr StringView BoolToAvailabilityText(const bool value)noexcept{
    return value ? StringView("available") : StringView("unavailable");
}

template<typename CharT>
[[nodiscard]] inline bool IsConfirmYesText(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    return text == BasicStringView<CharT>("y") || text == BasicStringView<CharT>("yes") || text == BasicStringView<CharT>("true") || text == BasicStringView<CharT>("1");
}

template<typename CharT>
[[nodiscard]] inline bool IsConfirmNoText(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    return text == BasicStringView<CharT>("n") || text == BasicStringView<CharT>("no") || text == BasicStringView<CharT>("false") || text == BasicStringView<CharT>("0");
}

template<typename CharT>
[[nodiscard]] inline Expected<bool> ParseConfirmText(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    if(IsConfirmYesText(text))
        return true;
    if(IsConfirmNoText(text))
        return false;
    return MakeUnexpected(Failure{});
}

template<typename CharT>
[[nodiscard]] inline BasicStringView<CharT> TruncateView(const BasicStringView<CharT> text, const usize maxChars)noexcept{
    return text.substr(0u, text.size() < maxChars ? text.size() : maxChars);
}

template<typename CharT>
[[nodiscard]] inline BasicStringView<CharT> TruncateView(const CharT* const text, const usize maxChars)noexcept{
    return text ? TruncateView(BasicStringView<CharT>(text), maxChars) : BasicStringView<CharT>();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_Utf8BomByteCount = 3u;
inline constexpr u8 s_Utf8BomByte0 = 0xEFu;
inline constexpr u8 s_Utf8BomByte1 = 0xBBu;
inline constexpr u8 s_Utf8BomByte2 = 0xBFu;
using BasicStringDetail::s_Utf8ContinuationMask;
using BasicStringDetail::s_Utf8ContinuationMarker;
using BasicStringDetail::s_Utf8OneByteMaxExclusive;
using BasicStringDetail::s_Utf8TwoByteMask;
using BasicStringDetail::s_Utf8TwoByteMarker;
using BasicStringDetail::s_Utf8ThreeByteMask;
using BasicStringDetail::s_Utf8ThreeByteMarker;
using BasicStringDetail::s_Utf8FourByteMask;
using BasicStringDetail::s_Utf8FourByteMarker;
using BasicStringDetail::s_Utf8TwoBytePayloadMask;
using BasicStringDetail::s_Utf8ThreeBytePayloadMask;
using BasicStringDetail::s_Utf8FourBytePayloadMask;
using BasicStringDetail::s_Utf8ContinuationPayloadMask;
using BasicStringDetail::s_Utf8ContinuationPayloadBits;
inline constexpr i32 s_Utf8TwoByteLength = 2;
inline constexpr i32 s_Utf8ThreeByteLength = 3;
inline constexpr i32 s_Utf8FourByteLength = 4;
using BasicStringDetail::s_Utf8TwoByteMinCodePoint;
using BasicStringDetail::s_Utf8ThreeByteMinCodePoint;
using BasicStringDetail::s_Utf8FourByteMinCodePoint;
using BasicStringDetail::s_UnicodeMaxCodePoint;
using BasicStringDetail::s_UnicodeHighSurrogateMin;
using BasicStringDetail::s_UnicodeLowSurrogateMax;
inline constexpr u32 s_AsciiControlMaxExclusive = 32u;
inline constexpr u32 s_AsciiDelete = 127u;
inline constexpr u8 s_JsonControlMaxExclusive = 0x20u;
inline constexpr usize s_DecimalTextBufferBytes = 32u;
inline constexpr i32 s_DefaultNumericTextBase = 10;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_trim_parse.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextUtilsDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT, typename PathT>
struct PathToStringArg{
    const PathT& path;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT = char, typename PathT>
[[nodiscard]] inline TextUtilsDetail::PathToStringArg<CharT, PathT> PathToString(const PathT& path)noexcept{
    return TextUtilsDetail::PathToStringArg<CharT, PathT>{ path };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename RequestedCharT, typename PathT, typename FormatCharT>
struct formatter<TextUtilsDetail::PathToStringArg<RequestedCharT, PathT>, FormatCharT>{
    constexpr auto parse(basic_format_parse_context<FormatCharT>& ctx)noexcept{
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(const TextUtilsDetail::PathToStringArg<RequestedCharT, PathT>& arg, FormatContext& ctx)const{
        auto out = ctx.out();
        BasicStringDetail::WriteConvertedText<FormatCharT>(out, arg.path.native());
        return out;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
inline constexpr CharT ToAsciiLower(CharT c)noexcept(IsArithmetic_V<CharT>){
    return (c >= static_cast<CharT>('A') && c <= static_cast<CharT>('Z'))
        ? static_cast<CharT>(c + (static_cast<CharT>('a') - static_cast<CharT>('A')))
        : c
    ;
}

template<typename CharT>
inline constexpr CharT ToAsciiUpper(CharT c)noexcept(IsArithmetic_V<CharT>){
    return (c >= static_cast<CharT>('a') && c <= static_cast<CharT>('z'))
        ? static_cast<CharT>(c - (static_cast<CharT>('a') - static_cast<CharT>('A')))
        : c
    ;
}

template<typename StringT>
inline void ToAsciiLowerInPlace(StringT& inOutText){
    for(auto& ch : inOutText)
        ch = ToAsciiLower(ch);
}

template<typename StringT>
[[nodiscard]] inline StringT ToAsciiLowerCopy(StringT text){
    ToAsciiLowerInPlace(text);
    return text;
}

template<typename StringT>
[[nodiscard]] inline StringT NormalizeOptionText(StringT text){
    return ToAsciiLowerCopy(TrimCopy(text));
}

template<typename StringT>
[[nodiscard]] inline StringT UnquoteMatchingAsciiQuotes(StringT text){
    TrimInPlace(text);
    if(text.size() >= 2u){
        const auto first = text.front();
        const auto last = text.back();
        if(
            (first == static_cast<decltype(first)>('"') && last == static_cast<decltype(last)>('"'))
            || (first == static_cast<decltype(first)>('\'') && last == static_cast<decltype(last)>('\''))
        ){
            text.erase(text.size() - 1u);
            text.erase(0u, 1u);
        }
    }
    return text;
}

template<typename CharT>
[[nodiscard]] inline BasicStringView<CharT> UnquoteDoubleQuotedView(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    if(text.size() < 2u || text.front() != static_cast<CharT>('"') || text.back() != static_cast<CharT>('"'))
        return BasicStringView<CharT>();
    return text.substr(1u, text.size() - 2u);
}

template<typename CharT>
[[nodiscard]] inline constexpr bool IsAsciiAlphaNumeric(CharT ch)noexcept(IsArithmetic_V<CharT>){
    return
        (ch >= static_cast<CharT>('0') && ch <= static_cast<CharT>('9'))
        || (ch >= static_cast<CharT>('A') && ch <= static_cast<CharT>('Z'))
        || (ch >= static_cast<CharT>('a') && ch <= static_cast<CharT>('z'))
    ;
}

template<typename CharT>
[[nodiscard]] inline constexpr bool IsAsciiIdentifierChar(CharT ch)noexcept(IsArithmetic_V<CharT>){
    return
        (ch >= static_cast<CharT>('a') && ch <= static_cast<CharT>('z'))
        || (ch >= static_cast<CharT>('A') && ch <= static_cast<CharT>('Z'))
        || (ch >= static_cast<CharT>('0') && ch <= static_cast<CharT>('9'))
        || ch == static_cast<CharT>('_')
    ;
}

template<typename CharT>
inline constexpr CharT Canonicalize(CharT c)noexcept(IsArithmetic_V<CharT>){
    if(c == static_cast<CharT>('\\'))
        return static_cast<CharT>('/');
    return ToAsciiLower(c);
}

template<typename CharT>
[[nodiscard]] inline constexpr bool EqualsAsciiIgnoreCase(const BasicStringView<CharT> text, const BasicStringView<CharT> expected)noexcept(IsArithmetic_V<CharT>){
    if(text == expected)
        return true;
    if(text.size() != expected.size())
        return false;

    for(usize i = 0u; i < text.size(); ++i){
        if(ToAsciiLower(text[i]) != ToAsciiLower(expected[i]))
            return false;
    }

    return true;
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline constexpr bool EqualsAsciiIgnoreCase(const BasicString<CharT, ArenaT>& text, const BasicStringView<CharT> expected)noexcept(IsArithmetic_V<CharT>){
    return EqualsAsciiIgnoreCase<CharT>(BasicStringView<CharT>{text}, expected);
}
template<typename CharT, usize N>
[[nodiscard]] inline constexpr bool EqualsAsciiIgnoreCase(const BasicStringView<CharT> text, const CharT (&expected)[N])noexcept(IsArithmetic_V<CharT>){
    return EqualsAsciiIgnoreCase<CharT>(text, BasicStringView<CharT>(expected, N > 0 ? N - 1 : 0));
}

template<typename CharT>
[[nodiscard]] inline constexpr bool ContainsAsciiIgnoreCase(const BasicStringView<CharT> text, const BasicStringView<CharT> expected)noexcept(IsArithmetic_V<CharT>){
    if(expected.empty())
        return true;
    if(text.size() < expected.size())
        return false;

    const usize lastBegin = text.size() - expected.size();
    for(usize begin = 0u; begin <= lastBegin; ++begin){
        bool matched = true;
        for(usize i = 0u; i < expected.size(); ++i){
            if(ToAsciiLower(text[begin + i]) != ToAsciiLower(expected[i])){
                matched = false;
                break;
            }
        }
        if(matched)
            return true;
    }
    return false;
}
template<typename CharT, usize N>
[[nodiscard]] inline constexpr bool ContainsAsciiIgnoreCase(const BasicStringView<CharT> text, const CharT (&expected)[N])noexcept(IsArithmetic_V<CharT>){
    return ContainsAsciiIgnoreCase<CharT>(text, BasicStringView<CharT>(expected, N > 0 ? N - 1 : 0));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextUtilsDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
inline constexpr bool IsSafeCacheNameChar(CharT ch)noexcept(IsArithmetic_V<CharT>){
    const bool alphaNum = (ch >= CharT('a') && ch <= CharT('z'))
        || (ch >= CharT('0') && ch <= CharT('9'));
    const bool safePunctuation = ch == CharT('.') || ch == CharT('_') || ch == CharT('-');
    return alphaNum || safePunctuation;
}

template<bool Canonical, typename StringT>
inline void SanitizeSafeCacheName(StringT& text){
    using CharT = typename StringT::value_type;
    for(CharT& ch : text){
        if constexpr(Canonical)
            ch = Canonicalize(ch);

        if(!IsSafeCacheNameChar(ch))
            ch = CharT('_');
    }
}

template<typename CharT, typename ArenaT, bool Canonical>
[[nodiscard]] inline BasicString<CharT, ArenaT> BuildSafeCacheNameImpl(ArenaT& arena, const BasicStringView<CharT> text){
    if(text.empty())
        return BasicString<CharT, ArenaT>(arena);

    BasicString<CharT, ArenaT> output(text.data(), text.size(), arena);
    SanitizeSafeCacheName<Canonical>(output);
    return output;
}

template<bool Canonical, typename StringT>
[[nodiscard]] inline StringT BuildSafeCacheNameCopy(const StringT& text){
    if(text.empty())
        return StringT(text.get_allocator());

    StringT output(text.data(), text.size(), text.get_allocator());
    SanitizeSafeCacheName<Canonical>(output);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicString<CharT, ArenaT> BuildSafeCacheName(ArenaT& arena, const BasicStringView<CharT> text){
    return TextUtilsDetail::BuildSafeCacheNameImpl<CharT, ArenaT, false>(arena, text);
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicString<CharT, ArenaT> BuildSafeCacheName(const BasicString<CharT, ArenaT>& text){
    return TextUtilsDetail::BuildSafeCacheNameCopy<false>(text);
}
template<typename StringT>
    requires requires(const StringT& text){
        typename StringT::value_type;
        text.empty();
        text.data();
        text.size();
        text.get_allocator();
    }
[[nodiscard]] inline StringT BuildSafeCacheName(const StringT& text){
    return TextUtilsDetail::BuildSafeCacheNameCopy<false>(text);
}


template<typename DstCharT, typename SrcCharT>
inline constexpr void CopyCanonical(DstCharT* dst, const usize dstSize, const BasicStringView<SrcCharT> src)noexcept(IsArithmetic_V<DstCharT> && IsArithmetic_V<SrcCharT>){
    if(dstSize == 0)
        return;

    usize writeIndex = 0;
    for(; writeIndex + 1 < dstSize && writeIndex < src.size() && src[writeIndex] != SrcCharT{}; ++writeIndex)
        dst[writeIndex] = static_cast<DstCharT>(Canonicalize(src[writeIndex]));
    dst[writeIndex] = DstCharT{};
}

template<typename DstCharT, typename SrcCharT>
inline constexpr void CopyCanonical(DstCharT* dst, const usize dstSize, const SrcCharT* src)noexcept(IsArithmetic_V<DstCharT> && IsArithmetic_V<SrcCharT>){
    CopyCanonical(dst, dstSize, src ? BasicStringView<SrcCharT>(src) : BasicStringView<SrcCharT>());
}

template<typename StringT>
inline void CanonicalizeTextInPlace(StringT& inOutText){
    for(auto& ch : inOutText)
        ch = Canonicalize(ch);
}

template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicString<CharT, ArenaT> CanonicalizeText(ArenaT& arena, const BasicStringView<CharT> text){
    if(text.empty())
        return BasicString<CharT, ArenaT>(arena);

    BasicString<CharT, ArenaT> output(text.data(), text.size(), arena);
    CanonicalizeTextInPlace(output);
    return output;
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicString<CharT, ArenaT> CanonicalizeText(const BasicString<CharT, ArenaT>& text){
    BasicString<CharT, ArenaT> output(text.data(), text.size(), text.get_allocator());
    CanonicalizeTextInPlace(output);
    return output;
}

template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicString<CharT, ArenaT> BuildCanonicalSafeCacheName(ArenaT& arena, const BasicStringView<CharT> text){
    return TextUtilsDetail::BuildSafeCacheNameImpl<CharT, ArenaT, true>(arena, text);
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicString<CharT, ArenaT> BuildCanonicalSafeCacheName(const BasicString<CharT, ArenaT>& text){
    return TextUtilsDetail::BuildSafeCacheNameCopy<true>(text);
}
template<typename StringT>
    requires requires(const StringT& text){
        typename StringT::value_type;
        text.empty();
        text.data();
        text.size();
        text.get_allocator();
    }
[[nodiscard]] inline StringT BuildCanonicalSafeCacheName(const StringT& text){
    return TextUtilsDetail::BuildSafeCacheNameCopy<true>(text);
}


template<typename CharT>
[[nodiscard]] inline bool HasEmbeddedNull(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    for(const CharT ch : text){
        if(ch == CharT{})
            return true;
    }
    return false;
}

template<typename CharT>
[[nodiscard]] inline bool HasLineBreak(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    for(const CharT ch : text){
        if(ch == CharT('\n') || ch == CharT('\r'))
            return true;
    }
    return false;
}

template<typename CharT>
[[nodiscard]] inline bool IsSingleLinePathText(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    return !text.empty() && !HasEmbeddedNull(text) && !HasLineBreak(text);
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline bool HasEmbeddedNull(const BasicString<CharT, ArenaT>& text)noexcept(IsArithmetic_V<CharT>){
    return HasEmbeddedNull(BasicStringView<CharT>(text));
}
template<typename StringT>
    requires requires(const StringT& text){
        typename StringT::value_type;
        text.data();
        text.size();
    }
[[nodiscard]] inline bool HasEmbeddedNull(const StringT& text)noexcept(noexcept(BasicStringView<typename StringT::value_type>(text.data(), text.size())) && IsArithmetic_V<typename StringT::value_type>){
    using CharT = typename StringT::value_type;
    return HasEmbeddedNull(BasicStringView<CharT>(text.data(), text.size()));
}


template<typename EnumT>
struct NamedEnumCase{
    AStringView text;
    EnumT value;
};

template<typename EnumT, typename ViewT = AStringView>
[[nodiscard]] inline Expected<EnumT> ParseNamedEnumText(
    const ViewT value,
    const NamedEnumCase<EnumT>* cases,
    const usize caseCount
){
    for(usize i = 0u; i < caseCount; ++i){
        if(value == ViewT(cases[i].text.data(), cases[i].text.size()))
            return cases[i].value;
    }
    return MakeUnexpected(Failure{});
}

template<typename EnumT, typename TextFunction, typename ViewT = AStringView>
[[nodiscard]] inline Expected<EnumT> ParseNormalizedEnumText(
    const ViewT value,
    TextFunction textFunction,
    const EnumT* values,
    const usize valueCount
){
    for(usize i = 0u; i < valueCount; ++i){
        if(value == textFunction(values[i]))
            return values[i];
    }
    return MakeUnexpected(Failure{});
}

template<typename StringT, typename EnumT, typename TextFunction>
[[nodiscard]] inline StringT BuildEnumOptionTexts(TextFunction textFunction, const EnumT* values, const usize valueCount){
    StringT text;
    for(usize i = 0u; i < valueCount; ++i){
        if(i > 0u)
            text += (i + 1u == valueCount) ? ", or " : ", ";
        text += textFunction(values[i]);
    }
    return text;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

