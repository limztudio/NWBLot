// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "limit.h"

#include <cctype>
#include <charconv>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline bool IsAsciiSpace(const CharT ch){
    return ch == CharT(' ') || ch == CharT('\t') || ch == CharT('\n') || ch == CharT('\r') || ch == CharT('\f') || ch == CharT('\v');
}

template<typename CharT>
[[nodiscard]] inline BasicStringView<CharT> SafeStringView(const CharT* const text)noexcept{
    return text ? BasicStringView<CharT>(text) : BasicStringView<CharT>();
}

[[nodiscard]] inline constexpr const char* BoolToYesNoText(const bool value)noexcept{
    return value ? "yes" : "no";
}

[[nodiscard]] inline constexpr const char* BoolToAvailabilityText(const bool value)noexcept{
    return value ? "available" : "unavailable";
}

template<typename CharT>
[[nodiscard]] inline bool IsConfirmYesText(const BasicStringView<CharT> text){
    return text == BasicStringView<CharT>("y") || text == BasicStringView<CharT>("yes") || text == BasicStringView<CharT>("true") || text == BasicStringView<CharT>("1");
}

template<typename CharT>
[[nodiscard]] inline bool IsConfirmNoText(const BasicStringView<CharT> text){
    return text == BasicStringView<CharT>("n") || text == BasicStringView<CharT>("no") || text == BasicStringView<CharT>("false") || text == BasicStringView<CharT>("0");
}

template<typename CharT>
[[nodiscard]] inline bool ParseConfirmText(const BasicStringView<CharT> text, bool& outValue){
    if(IsConfirmYesText(text)){
        outValue = true;
        return true;
    }
    if(IsConfirmNoText(text)){
        outValue = false;
        return true;
    }
    return false;
}

template<typename CharT>
[[nodiscard]] inline BasicStringView<CharT> TruncateView(const BasicStringView<CharT> text, const usize maxChars){
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
[[nodiscard]] inline TextUtilsDetail::PathToStringArg<CharT, PathT> PathToString(const PathT& path){
    return TextUtilsDetail::PathToStringArg<CharT, PathT>{ path };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename RequestedCharT, typename PathT, typename FormatCharT>
struct formatter<TextUtilsDetail::PathToStringArg<RequestedCharT, PathT>, FormatCharT>{
    constexpr auto parse(basic_format_parse_context<FormatCharT>& ctx){
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
inline constexpr CharT ToAsciiLower(CharT c){
    return (c >= static_cast<CharT>('A') && c <= static_cast<CharT>('Z'))
        ? static_cast<CharT>(c + (static_cast<CharT>('a') - static_cast<CharT>('A')))
        : c
    ;
}

template<typename CharT>
inline constexpr CharT ToAsciiUpper(CharT c){
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
[[nodiscard]] inline BasicStringView<CharT> UnquoteDoubleQuotedView(const BasicStringView<CharT> text){
    if(text.size() < 2u || text.front() != static_cast<CharT>('"') || text.back() != static_cast<CharT>('"'))
        return BasicStringView<CharT>();
    return text.substr(1u, text.size() - 2u);
}

template<typename CharT>
[[nodiscard]] inline constexpr bool IsAsciiAlphaNumeric(CharT ch){
    return
        (ch >= static_cast<CharT>('0') && ch <= static_cast<CharT>('9'))
        || (ch >= static_cast<CharT>('A') && ch <= static_cast<CharT>('Z'))
        || (ch >= static_cast<CharT>('a') && ch <= static_cast<CharT>('z'))
    ;
}

template<typename CharT>
[[nodiscard]] inline constexpr bool IsAsciiIdentifierChar(CharT ch){
    return
        (ch >= static_cast<CharT>('a') && ch <= static_cast<CharT>('z'))
        || (ch >= static_cast<CharT>('A') && ch <= static_cast<CharT>('Z'))
        || (ch >= static_cast<CharT>('0') && ch <= static_cast<CharT>('9'))
        || ch == static_cast<CharT>('_')
    ;
}

template<typename CharT>
inline constexpr CharT Canonicalize(CharT c){
    if(c == static_cast<CharT>('\\'))
        return static_cast<CharT>('/');
    return ToAsciiLower(c);
}

template<typename CharT>
[[nodiscard]] inline constexpr bool EqualsAsciiIgnoreCase(const BasicStringView<CharT> text, const BasicStringView<CharT> expected){
    if(text == expected)
        return true;
    if(text.size() != expected.size())
        return false;

    for(usize i = 0; i < text.size(); ++i){
        if(ToAsciiLower(text[i]) != ToAsciiLower(expected[i]))
            return false;
    }

    return true;
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline constexpr bool EqualsAsciiIgnoreCase(const BasicString<CharT, ArenaT>& text, const BasicStringView<CharT> expected){
    return EqualsAsciiIgnoreCase<CharT>(BasicStringView<CharT>{text}, expected);
}
template<typename CharT, usize N>
[[nodiscard]] inline constexpr bool EqualsAsciiIgnoreCase(const BasicStringView<CharT> text, const CharT (&expected)[N]){
    return EqualsAsciiIgnoreCase<CharT>(text, BasicStringView<CharT>(expected, N > 0 ? N - 1 : 0));
}

template<typename CharT>
[[nodiscard]] inline constexpr bool ContainsAsciiIgnoreCase(const BasicStringView<CharT> text, const BasicStringView<CharT> expected){
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
[[nodiscard]] inline constexpr bool ContainsAsciiIgnoreCase(const BasicStringView<CharT> text, const CharT (&expected)[N]){
    return ContainsAsciiIgnoreCase<CharT>(text, BasicStringView<CharT>(expected, N > 0 ? N - 1 : 0));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextUtilsDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
inline constexpr bool IsSafeCacheNameChar(CharT ch){
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
inline constexpr void CopyCanonical(DstCharT* dst, const usize dstSize, const SrcCharT* src){
    if(dstSize == 0)
        return;

    if(src == nullptr){
        dst[0] = DstCharT{};
        return;
    }

    usize writeIndex = 0;
    for(; writeIndex + 1 < dstSize && src[writeIndex] != SrcCharT{}; ++writeIndex)
        dst[writeIndex] = static_cast<DstCharT>(Canonicalize(src[writeIndex]));
    dst[writeIndex] = DstCharT{};
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
[[nodiscard]] inline bool HasEmbeddedNull(const BasicStringView<CharT> text){
    for(const CharT ch : text){
        if(ch == CharT{})
            return true;
    }
    return false;
}

template<typename CharT>
[[nodiscard]] inline bool HasLineBreak(const BasicStringView<CharT> text){
    for(const CharT ch : text){
        if(ch == CharT('\n') || ch == CharT('\r'))
            return true;
    }
    return false;
}

template<typename CharT>
[[nodiscard]] inline bool IsSingleLinePathText(const BasicStringView<CharT> text){
    return !text.empty() && !HasEmbeddedNull(text) && !HasLineBreak(text);
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline bool HasEmbeddedNull(const BasicString<CharT, ArenaT>& text){
    return HasEmbeddedNull(BasicStringView<CharT>(text));
}
template<typename StringT>
    requires requires(const StringT& text){
        typename StringT::value_type;
        text.data();
        text.size();
    }
[[nodiscard]] inline bool HasEmbeddedNull(const StringT& text){
    using CharT = typename StringT::value_type;
    return HasEmbeddedNull(BasicStringView<CharT>(text.data(), text.size()));
}


template<typename EnumT>
struct NamedEnumCase{
    AStringView text;
    EnumT value;
};

template<typename EnumT, typename ViewT = AStringView>
[[nodiscard]] inline bool ParseNamedEnumText(
    const ViewT value,
    EnumT& outValue,
    const NamedEnumCase<EnumT>* cases,
    const usize caseCount,
    const NamedEnumCase<EnumT>* aliasCases = nullptr,
    const usize aliasCaseCount = 0u
){
    for(usize i = 0u; i < caseCount; ++i){
        if(value == ViewT(cases[i].text.data(), cases[i].text.size())){
            outValue = cases[i].value;
            return true;
        }
    }
    for(usize i = 0u; i < aliasCaseCount; ++i){
        if(value == ViewT(aliasCases[i].text.data(), aliasCases[i].text.size())){
            outValue = aliasCases[i].value;
            return true;
        }
    }
    return false;
}

template<typename EnumT, typename TextFunction, typename ViewT = AStringView>
[[nodiscard]] inline bool ParseNormalizedEnumText(
    const ViewT value,
    EnumT& outValue,
    TextFunction textFunction,
    const EnumT* values,
    const usize valueCount,
    const EnumT fallback,
    const NamedEnumCase<EnumT>* aliases = nullptr,
    const usize aliasCount = 0u
){
    for(usize i = 0u; i < valueCount; ++i){
        if(value == textFunction(values[i])){
            outValue = values[i];
            return true;
        }
    }
    if(ParseNamedEnumText<EnumT, ViewT>(value, outValue, nullptr, 0u, aliases, aliasCount))
        return true;

    outValue = fallback;
    return false;
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


template<typename CharT, typename Container>
inline bool SplitText(const BasicStringView<CharT> line, const CharT delimiter, Container& outParts){
    outParts.clear();

    usize begin = 0;
    for(usize i = 0; i <= line.size(); ++i){
        const bool atEnd = i == line.size();
        if(!atEnd && line[i] != delimiter)
            continue;

        outParts.push_back(line.substr(begin, i - begin));
        begin = i + 1;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

