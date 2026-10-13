// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "expected.h"
#include "limit.h"
#include "numeric_text.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline bool IsAsciiSpace(const CharT ch)noexcept(IsArithmetic_V<CharT>){
    return ch == CharT(' ') || ch == CharT('\t') || ch == CharT('\n') || ch == CharT('\r') || ch == CharT('\f') || ch == CharT('\v');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_Utf8BomByteCount = 3u;
inline constexpr u8 s_Utf8BomByte0 = 0xEFu;
inline constexpr u8 s_Utf8BomByte1 = 0xBBu;
inline constexpr u8 s_Utf8BomByte2 = 0xBFu;
inline constexpr i32 s_Utf8TwoByteLength = 2;
inline constexpr i32 s_Utf8ThreeByteLength = 3;
inline constexpr i32 s_Utf8FourByteLength = 4;
inline constexpr u32 s_AsciiControlMaxExclusive = 32u;
inline constexpr u32 s_AsciiDelete = 127u;


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
template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicStringView<CharT> TrimView(const BasicString<CharT, ArenaT>& text)noexcept(IsArithmetic_V<CharT>){
    return TrimView<CharT>(BasicStringView<CharT>{text});
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
template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicStringView<CharT> TrimLeftView(const BasicString<CharT, ArenaT>& text)noexcept(IsArithmetic_V<CharT>){
    return TrimLeftView<CharT>(BasicStringView<CharT>{text});
}
template<typename StringT> requires requires(const StringT& text){ typename StringT::value_type; text.data(); text.size(); }
[[nodiscard]] inline BasicStringView<typename StringT::value_type> TrimLeftView(const StringT& text)noexcept(noexcept(BasicStringView<typename StringT::value_type>{text.data(), text.size()}) && IsArithmetic_V<typename StringT::value_type>){
    using CharT = typename StringT::value_type;
    return TrimLeftView<CharT>(BasicStringView<CharT>{text.data(), text.size()});
}
template<typename CharT, typename ArenaT>
BasicStringView<CharT> TrimLeftView(const BasicString<CharT, ArenaT>&&) = delete;


template<typename CharT>
[[nodiscard]] inline constexpr bool StartsWith(const BasicStringView<CharT> text, const BasicStringView<CharT> prefix)noexcept(IsArithmetic_V<CharT>){
    return text.size() >= prefix.size() && BasicStringView<CharT>(text.data(), prefix.size()) == prefix;
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline constexpr bool StartsWith(const BasicString<CharT, ArenaT>& text, const BasicStringView<CharT> prefix)noexcept(IsArithmetic_V<CharT>){
    return StartsWith<CharT>(BasicStringView<CharT>{text}, prefix);
}
template<typename CharT, usize N>
[[nodiscard]] inline constexpr bool StartsWith(const BasicStringView<CharT> text, const CharT (&prefix)[N])noexcept(IsArithmetic_V<CharT>){
    return StartsWith<CharT>(text, BasicStringView<CharT>(prefix, N > 0u ? N - 1u : 0u));
}
template<typename CharT, typename ArenaT, usize N>
[[nodiscard]] inline constexpr bool StartsWith(const BasicString<CharT, ArenaT>& text, const CharT (&prefix)[N])noexcept(IsArithmetic_V<CharT>){
    return StartsWith<CharT>(BasicStringView<CharT>{text}, BasicStringView<CharT>(prefix, N > 0u ? N - 1u : 0u));
}

template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicString<CharT, ArenaT> Trim(ArenaT& arena, const BasicStringView<CharT> text){
    return BasicString<CharT, ArenaT>(TrimView(text), arena);
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicString<CharT, ArenaT> Trim(const BasicString<CharT, ArenaT>& text){
    return BasicString<CharT, ArenaT>(TrimView<CharT>(BasicStringView<CharT>{text}), text.get_allocator());
}

template<typename StringT>
    requires requires(StringT& text, usize index, usize count){
        typename StringT::value_type;
        text.data();
        text.size();
        text.erase(index);
        text.erase(index, count);
    }
inline void TrimInPlace(StringT& inOutText){
    const BasicStringView<typename StringT::value_type> trimmed = TrimView(inOutText);
    if(trimmed.size() == inOutText.size())
        return;

    const usize trimBegin = static_cast<usize>(trimmed.data() - inOutText.data());
    inOutText.erase(trimBegin + trimmed.size());
    inOutText.erase(0u, trimBegin);
}

template<typename StringT>
[[nodiscard]] inline StringT TrimCopy(StringT text){
    TrimInPlace(text);
    return text;
}


template<typename CharT>
[[nodiscard]] inline bool HasCrlfLineEndings(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
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
inline void NormalizeLineEndingsInPlace(StringT& inOutText, const bool useCrlf){
    using CharT = typename StringT::value_type;
    const usize sourceSize = inOutText.size();

    if(!useCrlf){
        bool hasCarriageReturn = false;
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


template<typename StringT>
inline void StripUtf8Bom(StringT& inOutText){
    if(inOutText.size() < TextDetail::s_Utf8BomByteCount)
        return;

    const u8 byte0 = static_cast<u8>(inOutText[0]);
    const u8 byte1 = static_cast<u8>(inOutText[1]);
    const u8 byte2 = static_cast<u8>(inOutText[2]);
    const bool hasUtf8Bom = byte0 == TextDetail::s_Utf8BomByte0 && byte1 == TextDetail::s_Utf8BomByte1 && byte2 == TextDetail::s_Utf8BomByte2;
    if(hasUtf8Bom)
        inOutText.erase(0, TextDetail::s_Utf8BomByteCount);
}


[[nodiscard]] inline bool IsUtf8Continuation(const u8 value)noexcept{
    return (value & BasicStringDetail::s_Utf8ContinuationMask) == BasicStringDetail::s_Utf8ContinuationMarker;
}

struct DecodedUtf8CodePoint{
    u32 codePoint;
    i32 byteCount;
};

[[nodiscard]] inline Expected<DecodedUtf8CodePoint> DecodeUtf8CodePoint(const AStringView bytes)noexcept{
    u32 unicode = 0u;
    if(bytes.empty())
        return MakeUnexpected(Failure{});

    const u8 c0 = static_cast<u8>(bytes[0]);
    if(c0 < BasicStringDetail::s_Utf8OneByteMaxExclusive){
        unicode = c0;
        return DecodedUtf8CodePoint{ unicode, 1 };
    }

    if((c0 & BasicStringDetail::s_Utf8TwoByteMask) == BasicStringDetail::s_Utf8TwoByteMarker){
        if(bytes.size() < TextDetail::s_Utf8TwoByteLength)
            return MakeUnexpected(Failure{});

        const u8 c1 = static_cast<u8>(bytes[1]);
        if(!IsUtf8Continuation(c1))
            return MakeUnexpected(Failure{});

        unicode = (static_cast<u32>(c0 & BasicStringDetail::s_Utf8TwoBytePayloadMask) << BasicStringDetail::s_Utf8ContinuationPayloadBits) | static_cast<u32>(c1 & BasicStringDetail::s_Utf8ContinuationPayloadMask);
        if(unicode < BasicStringDetail::s_Utf8TwoByteMinCodePoint)
            return MakeUnexpected(Failure{});
        return DecodedUtf8CodePoint{ unicode, TextDetail::s_Utf8TwoByteLength };
    }

    if((c0 & BasicStringDetail::s_Utf8ThreeByteMask) == BasicStringDetail::s_Utf8ThreeByteMarker){
        if(bytes.size() < TextDetail::s_Utf8ThreeByteLength)
            return MakeUnexpected(Failure{});

        const u8 c1 = static_cast<u8>(bytes[1]);
        const u8 c2 = static_cast<u8>(bytes[2]);
        if(!IsUtf8Continuation(c1) || !IsUtf8Continuation(c2))
            return MakeUnexpected(Failure{});

        unicode =
            (static_cast<u32>(c0 & BasicStringDetail::s_Utf8ThreeBytePayloadMask) << (BasicStringDetail::s_Utf8ContinuationPayloadBits * 2u))
            | (static_cast<u32>(c1 & BasicStringDetail::s_Utf8ContinuationPayloadMask) << BasicStringDetail::s_Utf8ContinuationPayloadBits)
            | static_cast<u32>(c2 & BasicStringDetail::s_Utf8ContinuationPayloadMask)
        ;
        if(unicode < BasicStringDetail::s_Utf8ThreeByteMinCodePoint)
            return MakeUnexpected(Failure{});
        return DecodedUtf8CodePoint{ unicode, TextDetail::s_Utf8ThreeByteLength };
    }

    if((c0 & BasicStringDetail::s_Utf8FourByteMask) == BasicStringDetail::s_Utf8FourByteMarker){
        if(bytes.size() < TextDetail::s_Utf8FourByteLength)
            return MakeUnexpected(Failure{});

        const u8 c1 = static_cast<u8>(bytes[1]);
        const u8 c2 = static_cast<u8>(bytes[2]);
        const u8 c3 = static_cast<u8>(bytes[3]);
        if(!IsUtf8Continuation(c1) || !IsUtf8Continuation(c2) || !IsUtf8Continuation(c3))
            return MakeUnexpected(Failure{});

        unicode =
            (static_cast<u32>(c0 & BasicStringDetail::s_Utf8FourBytePayloadMask) << (BasicStringDetail::s_Utf8ContinuationPayloadBits * 3u))
            | (static_cast<u32>(c1 & BasicStringDetail::s_Utf8ContinuationPayloadMask) << (BasicStringDetail::s_Utf8ContinuationPayloadBits * 2u))
            | (static_cast<u32>(c2 & BasicStringDetail::s_Utf8ContinuationPayloadMask) << BasicStringDetail::s_Utf8ContinuationPayloadBits)
            | static_cast<u32>(c3 & BasicStringDetail::s_Utf8ContinuationPayloadMask)
        ;
        if(unicode < BasicStringDetail::s_Utf8FourByteMinCodePoint || unicode > BasicStringDetail::s_UnicodeMaxCodePoint)
            return MakeUnexpected(Failure{});
        return DecodedUtf8CodePoint{ unicode, TextDetail::s_Utf8FourByteLength };
    }

    return MakeUnexpected(Failure{});
}

[[nodiscard]] inline bool IsTextInputCodePoint(const u32 unicode)noexcept{
    if(unicode < TextDetail::s_AsciiControlMaxExclusive || unicode == TextDetail::s_AsciiDelete)
        return false;
    if(unicode > BasicStringDetail::s_UnicodeMaxCodePoint)
        return false;
    if(unicode >= BasicStringDetail::s_UnicodeHighSurrogateMin && unicode <= BasicStringDetail::s_UnicodeLowSurrogateMax)
        return false;
    return true;
}


template<typename CharT, typename Traits, typename AllocatorT>
[[nodiscard]] inline bool ReadTextLine(std::basic_istream<CharT, Traits>& stream, std::basic_string<CharT, Traits, AllocatorT>& outLine){
    return static_cast<bool>(std::getline(stream, outLine));
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
    static constexpr usize s_ByteTypeByteSize = 1u;
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

