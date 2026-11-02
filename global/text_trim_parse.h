// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "expected.h"
#include "limit.h"
#include "numeric_text.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_Utf8MaxSequenceBytes = 4u;
inline constexpr u32 s_UnicodeTab = 0x9u;
inline constexpr u32 s_UnicodeLineFeed = 0xAu;
inline constexpr u32 s_UnicodeCarriageReturn = 0xDu;
inline constexpr u32 s_UnicodeSpace = 0x20u;
inline constexpr u32 s_UnicodeDelete = 0x7Fu;
inline constexpr u32 s_UnicodeNextLine = 0x85u;
inline constexpr u32 s_UnicodeC1Last = 0x9Fu;


template<typename CharT>
[[nodiscard]] inline b8 IsAsciiSpace(const CharT ch)noexcept(IsArithmetic_V<CharT>){
    return ch == CharT(' ') || ch == CharT('\t') || ch == CharT('\n') || ch == CharT('\r') || ch == CharT('\f') || ch == CharT('\v');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_Utf8BomByteCount = 3u;
inline constexpr u8 s_Utf8BomByte0 = 0xEFu;
inline constexpr u8 s_Utf8BomByte1 = 0xBBu;
inline constexpr u8 s_Utf8BomByte2 = 0xBFu;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline BasicStringView<CharT> TrimView(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    usize begin = 0;
    while(begin < text.size() && IsAsciiSpace(text[begin]))
        ++begin;

    usize end = text.size();
    while(end > begin && IsAsciiSpace(text[end - 1]))
        --end;

    return text.substr(begin, end - begin);
}

template<typename StringT> requires requires(const StringT& text){ typename StringT::value_type; text.data(); text.size(); }
[[nodiscard]] inline BasicStringView<typename StringT::value_type> TrimView(const StringT& text)noexcept(noexcept(BasicStringView<typename StringT::value_type>{text.data(), text.size()}) && IsArithmetic_V<typename StringT::value_type>){
    using CharT = typename StringT::value_type;
    return TrimView<CharT>(BasicStringView<CharT>{text.data(), text.size()});
}
template<typename CharT, typename ArenaT>
BasicStringView<CharT> TrimView(const BasicString<CharT, ArenaT>&&) = delete;


template<typename CharT>
[[nodiscard]] inline BasicStringView<CharT> TrimLeftView(BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    while(!text.empty() && IsAsciiSpace(text.front()))
        text.remove_prefix(1u);
    return text;
}

template<typename StringT> requires requires(const StringT& text){ typename StringT::value_type; text.data(); text.size(); }
[[nodiscard]] inline BasicStringView<typename StringT::value_type> TrimLeftView(const StringT& text)noexcept(noexcept(BasicStringView<typename StringT::value_type>{text.data(), text.size()}) && IsArithmetic_V<typename StringT::value_type>){
    using CharT = typename StringT::value_type;
    return TrimLeftView<CharT>(BasicStringView<CharT>{text.data(), text.size()});
}
template<typename CharT, typename ArenaT>
BasicStringView<CharT> TrimLeftView(const BasicString<CharT, ArenaT>&&) = delete;


template<typename CharT>
[[nodiscard]] inline constexpr b8 StartsWith(const BasicStringView<CharT> text, const BasicStringView<CharT> prefix)noexcept(IsArithmetic_V<CharT>){
    return text.size() >= prefix.size() && BasicStringView<CharT>(text.data(), prefix.size()) == prefix;
}

template<typename CharT, usize N>
[[nodiscard]] inline constexpr b8 StartsWith(const BasicStringView<CharT> text, const CharT (&prefix)[N])noexcept(IsArithmetic_V<CharT>){
    return StartsWith<CharT>(text, BasicStringView<CharT>(prefix, N > 0u ? N - 1u : 0u));
}

template<typename StringT>
    requires requires(StringT& text, usize index, usize count){
        typename StringT::value_type;
        text.data();
        text.size();
        text.erase(index);
        text.erase(index, count);
    }
inline void TrimInPlace(StringT& inOutText)noexcept(
    noexcept(TrimView(inOutText)) && noexcept(static_cast<b8>(DeclVal<usize>() == inOutText.size()))
    && noexcept(static_cast<usize>(DeclVal<BasicStringView<typename StringT::value_type>>().data() - inOutText.data()))
    && noexcept(inOutText.erase(DeclVal<usize>())) && noexcept(inOutText.erase(0u, DeclVal<const usize&>()))
){
    const BasicStringView<typename StringT::value_type> trimmed = TrimView(inOutText);
    if(trimmed.size() == inOutText.size())
        return;

    const usize trimBegin = static_cast<usize>(trimmed.data() - inOutText.data());
    inOutText.erase(trimBegin + trimmed.size());
    inOutText.erase(0u, trimBegin);
}

template<typename StringT>
[[nodiscard]] inline StringT TrimCopy(StringT text)noexcept(noexcept(TrimInPlace(text)) && IsNothrowMoveConstructible_V<StringT> && IsNothrowDestructible_V<StringT>){
    TrimInPlace(text);
    return text;
}


template<typename CharT>
[[nodiscard]] inline b8 HasCrlfLineEndings(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    for(usize i = 1u; i < text.size(); ++i){
        if(text[i - 1u] == CharT('\r') && text[i] == CharT('\n'))
            return true;
    }
    return false;
}

template<typename StringT>
    requires requires(StringT& text, const usize size){
        typename StringT::value_type;
        text.size();
        text.resize(size);
        text[0u];
    }
inline void NormalizeLineEndingsInPlace(StringT& inOutText, const b8 useCrlf)noexcept([]()consteval noexcept{
    using CharT = typename StringT::value_type;
    return
        noexcept(DeclVal<void(*)(usize)noexcept>()(DeclVal<StringT&>().size()))
        && noexcept(static_cast<b8>(DeclVal<StringT&>()[DeclVal<usize&>()] == CharT('\r')))
        && noexcept(static_cast<b8>(DeclVal<b8>() && DeclVal<StringT&>()[DeclVal<usize>()] == CharT('\n')))
        && noexcept(DeclVal<void(*)(CharT)noexcept>()(DeclVal<StringT&>()[DeclVal<usize&>()]))
        && noexcept(static_cast<b8>(DeclVal<const CharT&>() == CharT('\r')))
        && noexcept(DeclVal<StringT&>()[DeclVal<usize>()] = CharT('\n'))
        && noexcept(DeclVal<StringT&>()[DeclVal<usize>()] = DeclVal<const CharT&>())
        && noexcept(DeclVal<StringT&>()[DeclVal<usize&>()] = CharT('\n'))
        && noexcept(DeclVal<StringT&>()[DeclVal<usize&>()] = DeclVal<const CharT&>())
        && noexcept(DeclVal<StringT&>().resize(DeclVal<usize&>())) && noexcept(DeclVal<StringT&>().resize(DeclVal<usize>()))
        && IsNothrowDestructible_V<CharT>
    ;
}()){
    using CharT = typename StringT::value_type;
    const usize sourceSize = inOutText.size();

    if(!useCrlf){
        b8 hasCarriageReturn = false;
        for(usize i = 0u; i < sourceSize; ++i){
            if(inOutText[i] == CharT('\r')){
                hasCarriageReturn = true;
                break;
            }
        }
        if(!hasCarriageReturn)
            return;

        usize write = 0u;
        for(usize read = 0u; read < sourceSize; ++read){
            const CharT ch = inOutText[read];
            if(ch == CharT('\r')){
                if(read + 1u < sourceSize && inOutText[read + 1u] == CharT('\n'))
                    ++read;
                inOutText[write++] = CharT('\n');
            }
            else
                inOutText[write++] = ch;
        }
        inOutText.resize(write);
        return;
    }

    usize growthCount = 0u;
    for(usize i = 0u; i < sourceSize; ++i){
        const CharT ch = inOutText[i];
        if(ch == CharT('\r')){
            if(i + 1u < sourceSize && inOutText[i + 1u] == CharT('\n'))
                ++i;
            else
                ++growthCount;
        }
        else if(ch == CharT('\n'))
            ++growthCount;
    }
    if(growthCount == 0u || growthCount > Limit<usize>::s_Max - sourceSize)
        return;

    inOutText.resize(sourceSize + growthCount);
    usize read = sourceSize;
    usize write = inOutText.size();
    while(read > 0u){
        --read;
        const CharT ch = inOutText[read];
        if(ch == CharT('\n')){
            if(read > 0u && inOutText[read - 1u] == CharT('\r'))
                --read;
            inOutText[--write] = CharT('\n');
            inOutText[--write] = CharT('\r');
        }
        else if(ch == CharT('\r')){
            inOutText[--write] = CharT('\n');
            inOutText[--write] = CharT('\r');
        }
        else
            inOutText[--write] = ch;
    }
}


template<typename ByteContainer>
[[nodiscard]] inline constexpr b8 HasUtf8Bom(const ByteContainer& bytes)noexcept(noexcept(static_cast<b8>(bytes.size() < TextDetail::s_Utf8BomByteCount)) && noexcept(static_cast<u8>(bytes[0u]))){
    if(bytes.size() < TextDetail::s_Utf8BomByteCount)
        return false;

    const u8 byte0 = static_cast<u8>(bytes[0u]);
    const u8 byte1 = static_cast<u8>(bytes[1u]);
    const u8 byte2 = static_cast<u8>(bytes[2u]);
    return byte0 == TextDetail::s_Utf8BomByte0 && byte1 == TextDetail::s_Utf8BomByte1 && byte2 == TextDetail::s_Utf8BomByte2;
}

template<typename StringT>
inline void StripUtf8Bom(StringT& inOutText)noexcept(noexcept(HasUtf8Bom(inOutText)) && noexcept(inOutText.erase(0, TextDetail::s_Utf8BomByteCount))){
    if(HasUtf8Bom(inOutText))
        inOutText.erase(0, TextDetail::s_Utf8BomByteCount);
}


[[nodiscard]] inline constexpr b8 IsUtf8ScalarBoundary(const AStringView text, const usize byteOffset)noexcept{
    return byteOffset <= text.size() && (byteOffset == text.size() || !IsUtf8Continuation(static_cast<u8>(text[byteOffset])));
}

struct DecodedUtf8CodePoint{
    u32 codePoint;
    i32 byteCount;
};

[[nodiscard]] inline Expected<DecodedUtf8CodePoint> DecodeUtf8CodePoint(const AStringView bytes)noexcept{
    DecodedUtf8CodePoint decoded{};
    if(!BasicStringDetail::TryDecodeUtf8CodePoint(decoded.codePoint, decoded.byteCount, bytes))
        return MakeUnexpected(Failure{});
    return decoded;
}

[[nodiscard]] inline Expected<DecodedUtf8CodePoint> DecodeUtf8Scalar(const AStringView bytes)noexcept{
    const auto decoded = DecodeUtf8CodePoint(bytes);
    if(!decoded || IsUnicodeSurrogate(decoded->codePoint))
        return MakeUnexpected(Failure{});
    return *decoded;
}

[[nodiscard]] inline constexpr Expected<usize> EncodeUtf8CodePoint(const u32 codePoint, const Span<char, s_Utf8MaxSequenceBytes> outBytes)noexcept{
    if(!IsUnicodeScalarValue(codePoint))
        return MakeUnexpected(Failure{});
    if(codePoint < BasicStringDetail::s_Utf8TwoByteMinCodePoint){
        outBytes[0u] = static_cast<char>(codePoint);
        return 1u;
    }
    if(codePoint < BasicStringDetail::s_Utf8ThreeByteMinCodePoint){
        outBytes[0u] = static_cast<char>(BasicStringDetail::s_Utf8TwoByteMarker | (codePoint >> BasicStringDetail::s_Utf8ContinuationPayloadBits));
        outBytes[1u] = static_cast<char>(BasicStringDetail::s_Utf8ContinuationMarker | (codePoint & BasicStringDetail::s_Utf8ContinuationPayloadMask));
        return 2u;
    }
    if(codePoint < BasicStringDetail::s_Utf8FourByteMinCodePoint){
        outBytes[0u] = static_cast<char>(BasicStringDetail::s_Utf8ThreeByteMarker | (codePoint >> (BasicStringDetail::s_Utf8ContinuationPayloadBits * 2u)));
        outBytes[1u] = static_cast<char>(BasicStringDetail::s_Utf8ContinuationMarker | ((codePoint >> BasicStringDetail::s_Utf8ContinuationPayloadBits) & BasicStringDetail::s_Utf8ContinuationPayloadMask));
        outBytes[2u] = static_cast<char>(BasicStringDetail::s_Utf8ContinuationMarker | (codePoint & BasicStringDetail::s_Utf8ContinuationPayloadMask));
        return 3u;
    }
    outBytes[0u] = static_cast<char>(BasicStringDetail::s_Utf8FourByteMarker | (codePoint >> (BasicStringDetail::s_Utf8ContinuationPayloadBits * 3u)));
    outBytes[1u] = static_cast<char>(BasicStringDetail::s_Utf8ContinuationMarker | ((codePoint >> (BasicStringDetail::s_Utf8ContinuationPayloadBits * 2u)) & BasicStringDetail::s_Utf8ContinuationPayloadMask));
    outBytes[2u] = static_cast<char>(BasicStringDetail::s_Utf8ContinuationMarker | ((codePoint >> BasicStringDetail::s_Utf8ContinuationPayloadBits) & BasicStringDetail::s_Utf8ContinuationPayloadMask));
    outBytes[3u] = static_cast<char>(BasicStringDetail::s_Utf8ContinuationMarker | (codePoint & BasicStringDetail::s_Utf8ContinuationPayloadMask));
    return 4u;
}

[[nodiscard]] inline constexpr b8 IsTextInputCodePoint(const u32 unicode)noexcept{
    if(unicode < s_UnicodeSpace || unicode == s_UnicodeDelete)
        return false;
    return IsUnicodeScalarValue(unicode);
}


template<typename CharT, typename Traits, typename AllocatorT>
[[nodiscard]] inline b8 ReadTextLine(std::basic_istream<CharT, Traits>& stream, std::basic_string<CharT, Traits, AllocatorT>& outLine){
    return static_cast<b8>(std::getline(stream, outLine));
}


template<typename CharT>
[[nodiscard]] inline Expected<BasicStringView<CharT>> NextTextLine(const BasicStringView<CharT> text, usize& inOutCursor)noexcept(IsArithmetic_V<CharT>){
    if(inOutCursor >= text.size())
        return MakeUnexpected(Failure{});

    const usize begin = inOutCursor;
    while(inOutCursor < text.size() && text[inOutCursor] != CharT('\n') && text[inOutCursor] != CharT('\r'))
        ++inOutCursor;

    const BasicStringView<CharT> line(text.data() + begin, inOutCursor - begin);
    while(inOutCursor < text.size() && (text[inOutCursor] == CharT('\n') || text[inOutCursor] == CharT('\r')))
        ++inOutCursor;
    return line;
}

template<typename ByteContainer>
[[nodiscard]] inline Expected<AStringView> NextLfByteLine(const ByteContainer& bytes, usize& inOutCursor)noexcept(
    IsArithmetic_V<typename ByteContainer::value_type>
    && IsSame_V<decltype(bytes.size()), usize>
    && IsArithmetic_V<RemoveCVRef_T<decltype(bytes[inOutCursor])>>
    && IsPointer_V<decltype(bytes.data())>
    && noexcept(bytes.size())
    && noexcept(bytes[inOutCursor])
    && noexcept(bytes.data())
){
    using ByteType = typename ByteContainer::value_type;
    static_assert(sizeof(ByteType) == s_ByteTypeByteSize, "NextLfByteLine requires a byte-sized container");

    if(inOutCursor >= bytes.size())
        return MakeUnexpected(Failure{});

    const usize begin = inOutCursor;
    while(inOutCursor < bytes.size() && bytes[inOutCursor] != static_cast<ByteType>('\n'))
        ++inOutCursor;
    if(inOutCursor >= bytes.size())
        return MakeUnexpected(Failure{});

    usize end = inOutCursor;
    ++inOutCursor;
    if(end > begin && bytes[end - 1u] == static_cast<ByteType>('\r'))
        --end;

    return AStringView(reinterpret_cast<const char*>(bytes.data() + begin), end - begin);
}

template<typename CharT>
[[nodiscard]] inline Expected<BasicStringView<CharT>> NextTrimmedTextLine(const BasicStringView<CharT> text, usize& inOutCursor)noexcept(IsArithmetic_V<CharT>){
    const auto line = NextTextLine(text, inOutCursor);
    if(!line)
        return MakeUnexpected(line.error());
    return TrimView(*line);
}

[[nodiscard]] inline Expected<AStringView> FindLineKeyValue(const AStringView text, const AStringView key)noexcept{
    usize cursor = 0u;
    while(const auto line = NextTextLine(text, cursor)){
        if(line->size() <= key.size() || !StartsWith(*line, key) || (*line)[key.size()] != '=')
            continue;
        return AStringView(line->data() + key.size() + 1u, line->size() - key.size() - 1u);
    }
    return MakeUnexpected(Failure{});
}

[[nodiscard]] inline Expected<u64> FindLineKeyValueU64(const AStringView text, const AStringView key)noexcept{
    const auto value = FindLineKeyValue(text, key);
    if(!value)
        return MakeUnexpected(value.error());
    return ParseU64(*value);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

