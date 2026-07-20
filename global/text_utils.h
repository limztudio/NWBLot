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

namespace TextDetail{
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
};


template<typename CharT>
[[nodiscard]] inline BasicStringView<CharT> TrimView(const BasicStringView<CharT> text){
    usize begin = 0;
    while(begin < text.size() && IsAsciiSpace(text[begin]))
        ++begin;

    usize end = text.size();
    while(end > begin && IsAsciiSpace(text[end - 1]))
        --end;

    return text.substr(begin, end - begin);
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicStringView<CharT> TrimView(const BasicString<CharT, ArenaT>& text){
    return TrimView<CharT>(BasicStringView<CharT>{text});
}
template<typename StringT> requires requires(const StringT& text){ typename StringT::value_type; text.data(); text.size(); }
[[nodiscard]] inline BasicStringView<typename StringT::value_type> TrimView(const StringT& text){
    using CharT = typename StringT::value_type;
    return TrimView<CharT>(BasicStringView<CharT>{text.data(), text.size()});
}
template<typename CharT, typename ArenaT>
BasicStringView<CharT> TrimView(const BasicString<CharT, ArenaT>&&) = delete;


template<typename CharT>
[[nodiscard]] inline BasicStringView<CharT> TrimLeftView(BasicStringView<CharT> text){
    while(!text.empty() && IsAsciiSpace(text.front()))
        text.remove_prefix(1u);
    return text;
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicStringView<CharT> TrimLeftView(const BasicString<CharT, ArenaT>& text){
    return TrimLeftView<CharT>(BasicStringView<CharT>{text});
}
template<typename StringT> requires requires(const StringT& text){ typename StringT::value_type; text.data(); text.size(); }
[[nodiscard]] inline BasicStringView<typename StringT::value_type> TrimLeftView(const StringT& text){
    using CharT = typename StringT::value_type;
    return TrimLeftView<CharT>(BasicStringView<CharT>{text.data(), text.size()});
}
template<typename CharT, typename ArenaT>
BasicStringView<CharT> TrimLeftView(const BasicString<CharT, ArenaT>&&) = delete;


template<typename CharT>
[[nodiscard]] inline constexpr bool StartsWith(const BasicStringView<CharT> text, const BasicStringView<CharT> prefix){
    return text.size() >= prefix.size() && BasicStringView<CharT>(text.data(), prefix.size()) == prefix;
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline constexpr bool StartsWith(const BasicString<CharT, ArenaT>& text, const BasicStringView<CharT> prefix){
    return StartsWith<CharT>(BasicStringView<CharT>{text}, prefix);
}
template<typename CharT, usize N>
[[nodiscard]] inline constexpr bool StartsWith(const BasicStringView<CharT> text, const CharT (&prefix)[N]){
    return StartsWith<CharT>(text, BasicStringView<CharT>(prefix, N > 0u ? N - 1u : 0u));
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


template<typename CharT, typename ArenaT>
inline void TrimTrailingCarriageReturn(BasicString<CharT, ArenaT>& inOutLine){
    if(!inOutLine.empty() && inOutLine.back() == CharT('\r'))
        inOutLine.pop_back();
}

template<typename CharT>
[[nodiscard]] inline bool HasCrlfLineEndings(const BasicStringView<CharT> text){
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
    const bool hasUtf8Bom =
        byte0 == TextDetail::s_Utf8BomByte0
        && byte1 == TextDetail::s_Utf8BomByte1
        && byte2 == TextDetail::s_Utf8BomByte2
    ;
    if(hasUtf8Bom)
        inOutText.erase(0, TextDetail::s_Utf8BomByteCount);
}


[[nodiscard]] inline bool IsUtf8Continuation(const u8 value){
    return (value & TextDetail::s_Utf8ContinuationMask) == TextDetail::s_Utf8ContinuationMarker;
}

[[nodiscard]] inline i32 DecodeUtf8CodePoint(const char* bytes, const i32 length, u32& unicode){
    if(length <= 0)
        return 0;

    const u8 c0 = static_cast<u8>(bytes[0]);
    if(c0 < TextDetail::s_Utf8OneByteMaxExclusive){
        unicode = c0;
        return 1;
    }

    if((c0 & TextDetail::s_Utf8TwoByteMask) == TextDetail::s_Utf8TwoByteMarker){
        if(length < TextDetail::s_Utf8TwoByteLength)
            return 0;

        const u8 c1 = static_cast<u8>(bytes[1]);
        if(!IsUtf8Continuation(c1))
            return 0;

        unicode = (static_cast<u32>(c0 & TextDetail::s_Utf8TwoBytePayloadMask) << TextDetail::s_Utf8ContinuationPayloadBits)
            | static_cast<u32>(c1 & TextDetail::s_Utf8ContinuationPayloadMask);
        return unicode >= TextDetail::s_Utf8TwoByteMinCodePoint ? TextDetail::s_Utf8TwoByteLength : 0;
    }

    if((c0 & TextDetail::s_Utf8ThreeByteMask) == TextDetail::s_Utf8ThreeByteMarker){
        if(length < TextDetail::s_Utf8ThreeByteLength)
            return 0;

        const u8 c1 = static_cast<u8>(bytes[1]);
        const u8 c2 = static_cast<u8>(bytes[2]);
        if(!IsUtf8Continuation(c1) || !IsUtf8Continuation(c2))
            return 0;

        unicode =
            (static_cast<u32>(c0 & TextDetail::s_Utf8ThreeBytePayloadMask) << (TextDetail::s_Utf8ContinuationPayloadBits * 2u))
            | (static_cast<u32>(c1 & TextDetail::s_Utf8ContinuationPayloadMask) << TextDetail::s_Utf8ContinuationPayloadBits)
            | static_cast<u32>(c2 & TextDetail::s_Utf8ContinuationPayloadMask)
        ;
        return unicode >= TextDetail::s_Utf8ThreeByteMinCodePoint ? TextDetail::s_Utf8ThreeByteLength : 0;
    }

    if((c0 & TextDetail::s_Utf8FourByteMask) == TextDetail::s_Utf8FourByteMarker){
        if(length < TextDetail::s_Utf8FourByteLength)
            return 0;

        const u8 c1 = static_cast<u8>(bytes[1]);
        const u8 c2 = static_cast<u8>(bytes[2]);
        const u8 c3 = static_cast<u8>(bytes[3]);
        if(!IsUtf8Continuation(c1) || !IsUtf8Continuation(c2) || !IsUtf8Continuation(c3))
            return 0;

        unicode =
            (static_cast<u32>(c0 & TextDetail::s_Utf8FourBytePayloadMask) << (TextDetail::s_Utf8ContinuationPayloadBits * 3u))
            | (static_cast<u32>(c1 & TextDetail::s_Utf8ContinuationPayloadMask) << (TextDetail::s_Utf8ContinuationPayloadBits * 2u))
            | (static_cast<u32>(c2 & TextDetail::s_Utf8ContinuationPayloadMask) << TextDetail::s_Utf8ContinuationPayloadBits)
            | static_cast<u32>(c3 & TextDetail::s_Utf8ContinuationPayloadMask)
        ;
        return unicode >= TextDetail::s_Utf8FourByteMinCodePoint && unicode <= TextDetail::s_UnicodeMaxCodePoint ? TextDetail::s_Utf8FourByteLength : 0;
    }

    return 0;
}

[[nodiscard]] inline bool IsTextInputCodePoint(const u32 unicode){
    if(unicode < TextDetail::s_AsciiControlMaxExclusive || unicode == TextDetail::s_AsciiDelete)
        return false;
    if(unicode > TextDetail::s_UnicodeMaxCodePoint)
        return false;
    if(unicode >= TextDetail::s_UnicodeHighSurrogateMin && unicode <= TextDetail::s_UnicodeLowSurrogateMax)
        return false;
    return true;
}


inline std::ios_base& StreamHex(std::ios_base& stream){ return std::hex(stream); }
inline std::ios_base& StreamDec(std::ios_base& stream){ return std::dec(stream); }


template<typename StringT>
inline void AppendJsonEscapedText(StringT& out, const AStringView text){
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
            if(static_cast<unsigned char>(ch) < TextDetail::s_JsonControlMaxExclusive)
                out += '?';
            else
                out += ch;
            break;
        }
    }
}

template<typename StringT>
inline void AppendJsonQuotedText(StringT& out, const AStringView text){
    out += '"';
    AppendJsonEscapedText(out, text);
    out += '"';
}

template<typename StringT>
[[nodiscard]] inline StringT MakeJsonEscapedText(const AStringView text){
    StringT out;
    out.reserve(text.size());
    AppendJsonEscapedText(out, text);
    return out;
}

template<typename StringT>
inline void AppendCsvCell(StringT& out, const AStringView text){
    bool quote = false;
    for(const char ch : text){
        if(ch == ',' || ch == '"' || ch == '\n' || ch == '\r'){
            quote = true;
            break;
        }
    }

    if(!quote){
        out.append(text.data(), text.size());
        return;
    }

    out += '"';
    for(const char ch : text){
        if(ch == '"')
            out += "\"\"";
        else
            out += ch;
    }
    out += '"';
}

template<typename StringT>
inline void AppendDotQuotedText(StringT& out, const AStringView text){
    out += '"';
    for(const char ch : text){
        if(ch == '\n'){
            out += "\\n";
            continue;
        }
        if(ch == '"' || ch == '\\')
            out += '\\';
        out += ch;
    }
    out += '"';
}


template<typename CharT, typename Traits, typename AllocatorT>
[[nodiscard]] inline bool ReadTextLine(std::basic_istream<CharT, Traits>& stream, std::basic_string<CharT, Traits, AllocatorT>& outLine){
    return static_cast<bool>(std::getline(stream, outLine));
}


template<usize N>
[[nodiscard]] inline AStringView FormatDecimal(const usize value, char (&buffer)[N]){
    const auto formatResult = std::to_chars(buffer, buffer + N, value);
    if(formatResult.ec != std::errc())
        return AStringView();

    return AStringView(buffer, static_cast<usize>(formatResult.ptr - buffer));
}


[[nodiscard]] inline bool ParseI64FromChars(const char* begin, const char* end, i64& outValue){
    const auto parseResult = std::from_chars(begin, end, outValue, 10);
    return parseResult.ec == std::errc() && parseResult.ptr == end;
}

[[nodiscard]] inline bool ParseU64FromChars(const char* begin, const char* end, u64& outValue){
    const auto parseResult = std::from_chars(begin, end, outValue, 10);
    return parseResult.ec == std::errc() && parseResult.ptr == end;
}

[[nodiscard]] inline bool ParseF64FromChars(const char* begin, const char* end, f64& outValue){
    const auto parseResult = std::from_chars(begin, end, outValue);
    return parseResult.ec == std::errc() && parseResult.ptr == end;
}

[[nodiscard]] inline bool ParseF32FromChars(const char* begin, const char* end, f32& outValue){
    const auto parseResult = std::from_chars(begin, end, outValue);
    return parseResult.ec == std::errc() && parseResult.ptr == end;
}


[[nodiscard]] inline bool ParseU64(const AStringView text, u64& outValue){
    outValue = 0;
    if(text.empty())
        return false;

    const char* begin = text.data();
    const char* end = begin + text.size();
    return ParseU64FromChars(begin, end, outValue);
}

template<typename CharT>
[[nodiscard]] inline bool NextTextLine(const BasicStringView<CharT> text, usize& inOutCursor, BasicStringView<CharT>& outLine){
    if(inOutCursor >= text.size())
        return false;

    const usize begin = inOutCursor;
    while(inOutCursor < text.size() && text[inOutCursor] != CharT('\n') && text[inOutCursor] != CharT('\r'))
        ++inOutCursor;

    outLine = BasicStringView<CharT>(text.data() + begin, inOutCursor - begin);
    while(inOutCursor < text.size() && (text[inOutCursor] == CharT('\n') || text[inOutCursor] == CharT('\r')))
        ++inOutCursor;
    return true;
}

template<typename ByteContainer>
[[nodiscard]] inline bool NextLfByteLine(const ByteContainer& bytes, usize& inOutCursor, AStringView& outLine){
    using ByteType = typename ByteContainer::value_type;
    static_assert(sizeof(ByteType) == 1u, "NextLfByteLine requires a byte-sized container");

    outLine = AStringView();
    if(inOutCursor >= bytes.size())
        return false;

    const usize begin = inOutCursor;
    while(inOutCursor < bytes.size() && bytes[inOutCursor] != static_cast<ByteType>('\n'))
        ++inOutCursor;
    if(inOutCursor >= bytes.size())
        return false;

    usize end = inOutCursor;
    ++inOutCursor;
    if(end > begin && bytes[end - 1u] == static_cast<ByteType>('\r'))
        --end;

    outLine = AStringView(reinterpret_cast<const char*>(bytes.data() + begin), end - begin);
    return true;
}

template<typename CharT>
[[nodiscard]] inline bool NextTrimmedTextLine(const BasicStringView<CharT> text, usize& inOutCursor, BasicStringView<CharT>& outLine){
    if(!NextTextLine(text, inOutCursor, outLine))
        return false;

    outLine = TrimView(outLine);
    return true;
}

[[nodiscard]] inline bool FindLineKeyValue(const AStringView text, const AStringView key, AStringView& outValue){
    outValue = AStringView();

    usize cursor = 0u;
    AStringView line;
    while(NextTextLine(text, cursor, line)){
        if(line.size() <= key.size() || !StartsWith(line, key) || line[key.size()] != '=')
            continue;

        outValue = AStringView(line.data() + key.size() + 1u, line.size() - key.size() - 1u);
        return true;
    }

    return false;
}

template<typename ArenaT>
[[nodiscard]] inline bool FindLineKeyValue(const AStringView text, const AStringView key, AString<ArenaT>& outValue){
    AStringView value;
    if(!FindLineKeyValue(text, key, value)){
        outValue.clear();
        return false;
    }

    outValue.assign(value.data(), value.size());
    return true;
}

[[nodiscard]] inline bool FindLineKeyValueU64(const AStringView text, const AStringView key, u64& outValue){
    AStringView value;
    return FindLineKeyValue(text, key, value) && ParseU64(value, outValue);
}


template<typename ArenaT>
[[nodiscard]] inline i32 Stoi(const AString<ArenaT>& str, usize* pos = nullptr, i32 base = 10){ return std::stoi(str, pos, base); }
template<typename ArenaT>
[[nodiscard]] inline i64 Stoll(const AString<ArenaT>& str, usize* pos = nullptr, i32 base = 10){ return std::stoll(str, pos, base); }
template<typename ArenaT>
[[nodiscard]] inline u64 Stoull(const AString<ArenaT>& str, usize* pos = nullptr, i32 base = 10){ return std::stoull(str, pos, base); }
template<typename ArenaT>
[[nodiscard]] inline f32 Stof(const AString<ArenaT>& str, usize* pos = nullptr){ return std::stof(str, pos); }
template<typename ArenaT>
[[nodiscard]] inline f64 Stod(const AString<ArenaT>& str, usize* pos = nullptr){ return std::stod(str, pos); }


template<typename CharT = char, typename ArenaT, typename PathT>
[[nodiscard]] inline BasicString<CharT, ArenaT> PathToString(ArenaT& arena, const PathT& path){
    if constexpr(SameAs<CharT, typename PathT::value_type>){
        const auto native = path.native();
        return BasicString<CharT, ArenaT>(native.data(), native.size(), arena);
    }
    else{
        BasicString<CharT, ArenaT> output{arena};
        auto out = std::back_inserter(output);
        BasicStringDetail::WriteConvertedText<CharT>(out, path.native());
        return output;
    }
}

template<typename CharT = char, typename ArenaT, typename PathT>
[[nodiscard]] inline BasicString<CharT, ArenaT> PathToGenericString(ArenaT& arena, const PathT& path){
    BasicString<CharT, ArenaT> output = PathToString<CharT>(arena, path);
    for(CharT& ch : output){
        if(ch == static_cast<CharT>('\\'))
            ch = static_cast<CharT>('/');
    }
    return output;
}


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
[[nodiscard]] inline constexpr bool IsAsciiAlphaNumeric(CharT ch){
    return
        (ch >= static_cast<CharT>('0') && ch <= static_cast<CharT>('9'))
        || (ch >= static_cast<CharT>('A') && ch <= static_cast<CharT>('Z'))
        || (ch >= static_cast<CharT>('a') && ch <= static_cast<CharT>('z'))
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

