// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "expected.h"
#include "range_traits.h"
#include "span.h"
#include "text_trim_parse.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline BasicStringView<CharT> SafeStringView(const CharT* const text)noexcept{
    return text ? BasicStringView<CharT>(text) : BasicStringView<CharT>();
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


template<usize N>
[[nodiscard]] inline constexpr AStringView LiteralView(const char (&text)[N])noexcept{
    return AStringView(text, N > 0u ? N - 1u : 0u);
}

template<typename CharT>
[[nodiscard]] inline constexpr b8 IsAsciiUpper(CharT ch)noexcept(IsArithmetic_V<CharT>){
    return ch >= static_cast<CharT>('A') && ch <= static_cast<CharT>('Z');
}

template<typename CharT>
[[nodiscard]] inline constexpr b8 IsAsciiLower(CharT ch)noexcept(IsArithmetic_V<CharT>){
    return ch >= static_cast<CharT>('a') && ch <= static_cast<CharT>('z');
}

template<typename CharT>
inline constexpr CharT ToAsciiLower(CharT c)noexcept(IsArithmetic_V<CharT>){
    return IsAsciiUpper(c) ? static_cast<CharT>(c + (static_cast<CharT>('a') - static_cast<CharT>('A'))) : c;
}

[[nodiscard]] inline constexpr u8 ToAsciiLowerByte(const u8 value)noexcept{
    return value >= static_cast<u8>('A') && value <= static_cast<u8>('Z') ? static_cast<u8>(value + ('a' - 'A')) : value;
}

template<typename CharT>
inline constexpr CharT ToAsciiUpper(CharT c)noexcept(IsArithmetic_V<CharT>){
    return IsAsciiLower(c) ? static_cast<CharT>(c - (static_cast<CharT>('a') - static_cast<CharT>('A'))) : c;
}

template<typename StringT>
inline void ToAsciiLowerInPlace(StringT& inOutText)noexcept(IsNothrowRangeFor<StringT, decltype([](auto& ch)noexcept(noexcept(ch = ToAsciiLower(ch))){ ch = ToAsciiLower(ch); })>()){
    for(auto& ch : inOutText)
        ch = ToAsciiLower(ch);
}

template<typename StringT>
[[nodiscard]] inline StringT UnquoteMatchingAsciiQuotes(StringT text)noexcept([]()consteval noexcept{
    using First = const Decay_T<decltype(DeclVal<StringT&>().front())>;
    using Last = const Decay_T<decltype(DeclVal<StringT&>().back())>;
    return
        noexcept(TrimInPlace(DeclVal<StringT&>())) && noexcept(static_cast<b8>(DeclVal<StringT&>().size() >= 2u))
        && noexcept(DeclVal<void(*)(First)noexcept>()(DeclVal<StringT&>().front())) && noexcept(DeclVal<void(*)(Last)noexcept>()(DeclVal<StringT&>().back()))
        && noexcept(static_cast<b8>((DeclVal<First&>() == static_cast<First>('"') && DeclVal<Last&>() == static_cast<Last>('"')) || (DeclVal<First&>() == static_cast<First>('\'') && DeclVal<Last&>() == static_cast<Last>('\''))))
        && noexcept(DeclVal<StringT&>().erase(DeclVal<StringT&>().size() - 1u)) && noexcept(DeclVal<StringT&>().erase(0u, 1u))
        && IsNothrowDestructible_V<First> && IsNothrowDestructible_V<Last> && IsNothrowMoveConstructible_V<StringT> && IsNothrowDestructible_V<StringT>
    ;
}()){
    TrimInPlace(text);
    if(text.size() >= 2u){
        const auto first = text.front();
        const auto last = text.back();
        if((first == static_cast<decltype(first)>('"') && last == static_cast<decltype(last)>('"')) || (first == static_cast<decltype(first)>('\'') && last == static_cast<decltype(last)>('\''))){
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
[[nodiscard]] inline constexpr b8 IsAsciiDigit(CharT ch)noexcept(IsArithmetic_V<CharT>){
    return ch >= static_cast<CharT>('0') && ch <= static_cast<CharT>('9');
}

[[nodiscard]] inline constexpr b8 IsHexDigit(const char c)noexcept{
    return IsAsciiDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

[[nodiscard]] inline constexpr u32 HexDigitValue(const char c)noexcept{
    return IsAsciiDigit(c) ? static_cast<u32>(c - '0') : static_cast<u32>((c >= 'a' && c <= 'f') ? (c - 'a' + 10) : (c - 'A' + 10));
}

[[nodiscard]] inline constexpr b8 IsBinaryDigit(const char c)noexcept{
    return c == '0' || c == '1';
}

[[nodiscard]] inline constexpr b8 IsOctalDigit(const char c)noexcept{
    return c >= '0' && c <= '7';
}

template<typename CharT>
[[nodiscard]] inline constexpr b8 IsAsciiAlpha(CharT ch)noexcept(IsArithmetic_V<CharT>){
    return IsAsciiUpper(ch) || IsAsciiLower(ch);
}

template<typename CharT>
[[nodiscard]] inline constexpr b8 IsAsciiAlphaNumeric(CharT ch)noexcept(IsArithmetic_V<CharT>){
    return IsAsciiDigit(ch) || IsAsciiAlpha(ch);
}

template<typename CharT>
[[nodiscard]] inline constexpr b8 IsAsciiIdentifierStart(CharT ch)noexcept(IsArithmetic_V<CharT>){
    return IsAsciiAlpha(ch) || ch == static_cast<CharT>('_');
}

template<typename CharT>
[[nodiscard]] inline constexpr b8 IsAsciiIdentifierChar(CharT ch)noexcept(IsArithmetic_V<CharT>){
    return IsAsciiIdentifierStart(ch) || IsAsciiDigit(ch);
}

template<typename CharT = char>
[[nodiscard]] inline constexpr b8 IsAsciiIdentifier(const TypeIdentity<BasicStringView<CharT>> text)noexcept(IsArithmetic_V<CharT>){
    if(text.empty() || !IsAsciiIdentifierStart(text.front()))
        return false;
    for(usize i = 1u; i < text.size(); ++i){
        if(!IsAsciiIdentifierChar(text[i]))
            return false;
    }
    return true;
}

[[nodiscard]] inline constexpr AStringView ExtractLeadingIdentifier(const AStringView text)noexcept{
    usize nameEnd = 0u;
    while(nameEnd < text.size() && IsAsciiIdentifierChar(text[nameEnd]))
        ++nameEnd;
    return text.substr(0u, nameEnd);
}

// The index must not exceed the view length.
[[nodiscard]] inline constexpr b8 IsEscapedQuoteAt(const AStringView text, const usize quoteIdx)noexcept{
    usize backslashes = 0u;
    usize pos = quoteIdx;
    while(pos > 0u && text[pos - 1u] == '\\'){
        ++backslashes;
        --pos;
    }
    return (backslashes & 1u) != 0u;
}

template<typename CharT>
inline constexpr CharT Canonicalize(CharT c)noexcept(IsArithmetic_V<CharT>){
    if(c == static_cast<CharT>('\\'))
        return static_cast<CharT>('/');
    return ToAsciiLower(c);
}

template<typename CharT>
[[nodiscard]] inline constexpr b8 EqualsAsciiIgnoreCase(const BasicStringView<CharT> text, const BasicStringView<CharT> expected)noexcept(IsArithmetic_V<CharT>){
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

template<typename CharT, usize N>
[[nodiscard]] inline constexpr b8 EqualsAsciiIgnoreCase(const BasicStringView<CharT> text, const CharT (&expected)[N])noexcept(IsArithmetic_V<CharT>){
    return EqualsAsciiIgnoreCase<CharT>(text, BasicStringView<CharT>(expected, N > 0 ? N - 1 : 0));
}

template<typename CharT>
[[nodiscard]] inline constexpr b8 ContainsAsciiIgnoreCase(const BasicStringView<CharT> text, const BasicStringView<CharT> expected)noexcept(IsArithmetic_V<CharT>){
    if(expected.empty())
        return true;
    if(text.size() < expected.size())
        return false;

    const usize lastBegin = text.size() - expected.size();
    for(usize begin = 0u; begin <= lastBegin; ++begin){
        b8 matched = true;
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
[[nodiscard]] inline constexpr b8 ContainsAsciiIgnoreCase(const BasicStringView<CharT> text, const CharT (&expected)[N])noexcept(IsArithmetic_V<CharT>){
    return ContainsAsciiIgnoreCase<CharT>(text, BasicStringView<CharT>(expected, N > 0 ? N - 1 : 0));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The input must remain valid while the output grows.
template<typename StringT>
inline void AppendTextDiscardCrAndExpandLf(StringT& out, const AStringView text)noexcept(noexcept(out.append("\r\n")) && noexcept(out.push_back(char{}))){
    for(usize i = 0u; i < text.size(); ++i){
        const char ch = text[i];
        if(ch == '\r')
            continue;
        if(ch == '\n')
            out.append("\r\n");
        else
            out.push_back(ch);
    }
}

[[nodiscard]] inline constexpr AStringView BoolToYesNoText(const b8 value)noexcept{
    return value ? AStringView("yes") : AStringView("no");
}

template<typename ArenaT>
[[nodiscard]] inline AString<ArenaT> JoinWithSingleSlash(ArenaT& arena, const AStringView base, const AStringView suffix){
    AString<ArenaT> output(AStringView(base.data(), base.size()), arena);
    if(output.empty() || suffix.empty())
        return output;
    const b8 baseHasSlash = output.back() == '/';
    const b8 suffixHasSlash = suffix.front() == '/';
    if(baseHasSlash && suffixHasSlash)
        output.pop_back();
    else if(!baseHasSlash && !suffixHasSlash)
        output += '/';
    output.append(suffix.data(), suffix.size());
    return output;
}

template<typename StringT>
inline void CollapseCrLfInPlace(StringT& text)noexcept(noexcept(text.size()) && noexcept(text[usize{}] = text[usize{}]) && noexcept(text.resize(usize{}))){
    usize destination = 0u;
    for(usize source = 0u; source < text.size(); ++source){
        if(text[source] == '\r' && source + 1u < text.size() && text[source + 1u] == '\n')
            continue;
        text[destination++] = text[source];
    }
    text.resize(destination);
}

template<typename CharT>
[[nodiscard]] inline usize CountBareLineFeeds(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    usize count = 0u;
    for(usize index = 0u; index < text.size(); ++index){
        if(text[index] == CharT('\n') && (index == 0u || text[index - 1u] != CharT('\r')))
            ++count;
    }
    return count;
}

// The destination must have enough space and not overlap the input.
template<typename CharT>
[[nodiscard]] inline usize CopyWithCrBeforeBareLf(const BasicStringView<CharT> text, CharT* const destination)noexcept(IsArithmetic_V<CharT>){
    usize output = 0u;
    for(usize index = 0u; index < text.size(); ++index){
        if(text[index] == CharT('\n') && (index == 0u || text[index - 1u] != CharT('\r')))
            destination[output++] = CharT('\r');
        destination[output++] = text[index];
    }
    return output;
}

[[nodiscard]] inline b8 IsRelativeSlashPathWithoutDotSegments(const AStringView path)noexcept{
    if(path.empty() || path[0u] == '/' || path[path.size() - 1u] == '/')
        return false;
    usize segmentStart = 0u;
    for(usize i = 0u; i <= path.size(); ++i){
        if(i < path.size() && path[i] == '\\')
            return false;
        if(i == path.size() || path[i] == '/'){
            const usize segmentLength = i - segmentStart;
            if(segmentLength == 0u)
                return false;
            const AStringView segment = path.substr(segmentStart, segmentLength);
            if(segment == "." || segment == "..")
                return false;
            segmentStart = i + 1u;
        }
    }
    return true;
}

[[nodiscard]] inline AStringView StripMatchingTrailingByte(const AStringView text, const u8 first, const u8 second)noexcept{
    if(text.size() <= 1u)
        return text;
    const u8 suffix = static_cast<u8>(text[text.size() - 1u]);
    return suffix == first || suffix == second ? text.substr(0u, text.size() - 1u) : text;
}

[[nodiscard]] inline Expected<AStringView> ReadCommaOrSpaceDelimitedToken(const AStringView text, usize& cursor)noexcept{
    while(cursor < text.size() && (IsAsciiSpace(text[cursor]) || text[cursor] == ','))
        ++cursor;
    if(cursor >= text.size())
        return MakeUnexpected(Failure{});
    const usize begin = cursor;
    while(cursor < text.size() && !IsAsciiSpace(text[cursor]) && text[cursor] != ',')
        ++cursor;
    const AStringView token = TrimView(text.substr(begin, cursor - begin));
    if(token.empty())
        return MakeUnexpected(Failure{});
    return token;
}

[[nodiscard]] inline constexpr b8 IsUnicodeWhitespace(const u32 scalar)noexcept{
    return (scalar >= 0x09u && scalar <= 0x0du) || scalar == 0x20u || scalar == 0x85u || scalar == 0xa0u
        || scalar == 0x1680u || (scalar >= 0x2000u && scalar <= 0x200au) || scalar == 0x2028u || scalar == 0x2029u
        || scalar == 0x202fu || scalar == 0x205fu || scalar == 0x3000u;
}

[[nodiscard]] inline constexpr b8 IsAsciiPunctuation(const u32 scalar)noexcept{
    return (scalar >= 0x21u && scalar <= 0x2fu) || (scalar >= 0x3au && scalar <= 0x40u)
        || (scalar >= 0x5bu && scalar <= 0x60u) || (scalar >= 0x7bu && scalar <= 0x7eu);
}

// Fallback is null-terminated; both inputs must remain valid while the output grows.
template<typename StringT>
void AppendAsciiUpperIdentifier(const AStringView text, const char* const fallback, StringT& inOutText)noexcept(
    noexcept(DeclVal<void(*)(usize)noexcept>()(inOutText.size()))
    && noexcept(static_cast<b8>(inOutText.size() == DeclVal<const usize&>()))
    && noexcept(inOutText += char{})
    && noexcept(inOutText += static_cast<const char*>(fallback))
){
    const usize beginSize = inOutText.size();
    for(const char ch : text)
        inOutText += IsAsciiAlphaNumeric(ch) ? ToAsciiUpper(ch) : '_';
    if(inOutText.size() == beginSize)
        inOutText += static_cast<const char*>(fallback);
}

template<typename StringT>
void AppendUnderscoreSeparatedPascalIdentifier(const AStringView text, const char* const fallback, StringT& inOutText)noexcept(
    noexcept(DeclVal<void(*)(usize)noexcept>()(inOutText.size()))
    && noexcept(static_cast<b8>(inOutText.size() == DeclVal<const usize&>()))
    && noexcept(inOutText += char{})
    && noexcept(inOutText += DeclVal<const char&>())
    && noexcept(inOutText += static_cast<const char*>(fallback))
){
    const usize beginSize = inOutText.size();
    b8 upperNext = true;
    for(const char ch : text){
        if(ch == '_'){
            upperNext = true;
            continue;
        }

        if(upperNext)
            inOutText += ToAsciiUpper(ch);
        else
            inOutText += ch;
        upperNext = false;
    }
    if(inOutText.size() == beginSize)
        inOutText += static_cast<const char*>(fallback);
}


template<typename StringT>
inline void AppendPascalCaseIdentifierSegment(const AStringView segment, StringT& out)noexcept(noexcept(out.push_back(char{}))){
    b8 capitalizeNext = true;
    b8 emittedAny = false;
    for(const char c : segment){
        const b8 isLower = IsAsciiLower(c);
        const b8 isUpper = IsAsciiUpper(c);
        const b8 isDigit = IsAsciiDigit(c);
        if(!isLower && !isUpper && !isDigit){
            capitalizeNext = true;
            continue;
        }
        if(!emittedAny && isDigit)
            out.push_back('_');
        out.push_back(capitalizeNext && isLower ? static_cast<char>(c - ('a' - 'A')) : c);
        capitalizeNext = false;
        emittedAny = true;
    }
}

template<typename StringT>
inline void AppendDepfileEscapedPath(const AStringView pathText, StringT& out)noexcept(noexcept(out.push_back(char{}))){
    for(usize index = 0u; index < pathText.size(); ++index){
        const char c = pathText[index];
        if(c == '\\'){
            const usize begin = index;
            while(index < pathText.size() && pathText[index] == '\\')
                ++index;
            const usize count = index - begin;
            for(usize slash = 0u; slash < count; ++slash)
                out.push_back('\\');
            if(index < pathText.size() && pathText[index] == ' '){
                for(usize slash = 0u; slash < count; ++slash)
                    out.push_back('\\');
            }
            --index;
            continue;
        }
        if(c == ' ' || c == '#' || c == ':')
            out.push_back('\\');
        else if(c == '$')
            out.push_back('$');
        out.push_back(c);
    }
}

template<typename StringT>
inline void AppendCamelToSnakeCase(StringT& out, const AStringView text)noexcept(noexcept(out.push_back(char{}))){
    for(usize i = 0u; i < text.size(); ++i){
        const char ch = text[i];
        if(IsAsciiUpper(ch)){
            if(i > 0u){
                const char prev = text[i - 1u];
                const b8 prevLowerOrDigit = IsAsciiLower(prev) || IsAsciiDigit(prev);
                const b8 nextLower = (i + 1u < text.size()) && IsAsciiLower(text[i + 1u]);
                if(prevLowerOrDigit || nextLower)
                    out.push_back('_');
            }
            out.push_back(static_cast<char>(ch + ('a' - 'A')));
        }
        else{
            out.push_back(ch);
        }
    }
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
inline void CanonicalizeTextInPlace(StringT& inOutText)noexcept(IsNothrowRangeFor<StringT, decltype([](auto& ch)noexcept(noexcept(ch = Canonicalize(ch))){ ch = Canonicalize(ch); })>()){
    for(auto& ch : inOutText)
        ch = Canonicalize(ch);
}

template<typename CharT>
[[nodiscard]] inline b8 HasEmbeddedNull(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    for(const CharT ch : text){
        if(ch == CharT{})
            return true;
    }
    return false;
}

template<StringViewConvertible StringT>
[[nodiscard]] inline b8 HasEmbeddedNull(const StringT& text)noexcept(noexcept(AsBasicStringView(text)) && IsArithmetic_V<typename StringT::value_type>){
    return HasEmbeddedNull(AsBasicStringView(text));
}

[[nodiscard]] inline b8 FitsFixedText(const AStringView text, const usize capacity)noexcept{
    return text.size() < capacity && !HasEmbeddedNull(text);
}

template<typename CharT>
[[nodiscard]] inline b8 HasLineBreak(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    for(const CharT ch : text){
        if(ch == CharT('\n') || ch == CharT('\r'))
            return true;
    }
    return false;
}

template<typename EnumT, typename TextFunction>
[[nodiscard]] inline Expected<EnumT> ParseOptionText(const AStringView value, const Span<const EnumT> values, TextFunction textFunction)noexcept(noexcept(textFunction(values[usize{}])) && noexcept(Expected<EnumT>(values[usize{}]))){
    const AStringView trimmed = TrimView(value);
    for(usize i = 0u; i < values.size(); ++i){
        if(EqualsAsciiIgnoreCase(trimmed, textFunction(values[i])))
            return values[i];
    }
    return MakeUnexpected(Failure{});
}

template<typename EnumT>
struct NamedEnumCase{
    AStringView text;
    EnumT value;
};

template<typename EnumT, typename ViewT = AStringView>
[[nodiscard]] inline Expected<EnumT> ParseNamedEnumText(const ViewT value, const Span<const NamedEnumCase<EnumT>> cases)noexcept(
    noexcept(static_cast<b8>(value == ViewT(cases[DeclVal<usize&>()].text.data(), cases[DeclVal<usize&>()].text.size())))
    && noexcept(Expected<EnumT>(cases[DeclVal<usize&>()].value))
){
    for(const NamedEnumCase<EnumT>& item : cases){
        if(value == ViewT(item.text.data(), item.text.size()))
            return item.value;
    }
    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

