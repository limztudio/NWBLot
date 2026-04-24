// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"

#include <cctype>
#include <charconv>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline bool IsAsciiSpace(const CharT ch){
    return ch == CharT(' ') || ch == CharT('\t') || ch == CharT('\n')
        || ch == CharT('\r') || ch == CharT('\f') || ch == CharT('\v');
}


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
template<typename CharT>
[[nodiscard]] inline BasicStringView<CharT> TrimView(const BasicString<CharT>& text){
    return TrimView<CharT>(BasicStringView<CharT>{text});
}
template<typename CharT>
BasicStringView<CharT> TrimView(const BasicString<CharT>&&) = delete;


template<typename CharT>
[[nodiscard]] inline BasicString<CharT> Trim(const BasicStringView<CharT> text){
    return BasicString<CharT>(TrimView(text));
}
template<typename CharT>
[[nodiscard]] inline BasicString<CharT> Trim(const BasicString<CharT>& text){
    return Trim<CharT>(BasicStringView<CharT>{text});
}


template<typename CharT>
inline void TrimTrailingCarriageReturn(BasicString<CharT>& inOutLine){
    if(!inOutLine.empty() && inOutLine.back() == CharT('\r'))
        inOutLine.pop_back();
}


inline void StripUtf8Bom(AString& inOutText){
    if(inOutText.size() < 3)
        return;

    const u8 byte0 = static_cast<u8>(inOutText[0]);
    const u8 byte1 = static_cast<u8>(inOutText[1]);
    const u8 byte2 = static_cast<u8>(inOutText[2]);
    const bool hasUtf8Bom = byte0 == 0xEF && byte1 == 0xBB && byte2 == 0xBF;
    if(hasUtf8Bom)
        inOutText.erase(0, 3);
}


inline std::ios_base& StreamHex(std::ios_base& stream){ return std::hex(stream); }
inline std::ios_base& StreamDec(std::ios_base& stream){ return std::dec(stream); }


template<typename CharT>
[[nodiscard]] inline bool ReadTextLine(BasicStringStream<CharT>& stream, BasicString<CharT>& outLine){
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


[[nodiscard]] inline bool ParseU64(const AStringView text, u64& outValue){
    outValue = 0;
    if(text.empty())
        return false;

    const char* begin = text.data();
    const char* end = begin + text.size();
    return ParseU64FromChars(begin, end, outValue);
}


[[nodiscard]] inline i32 Stoi(const AString& str, usize* pos = nullptr, i32 base = 10){ return std::stoi(str, pos, base); }
[[nodiscard]] inline i64 Stoll(const AString& str, usize* pos = nullptr, i32 base = 10){ return std::stoll(str, pos, base); }
[[nodiscard]] inline u64 Stoull(const AString& str, usize* pos = nullptr, i32 base = 10){ return std::stoull(str, pos, base); }
[[nodiscard]] inline f32 Stof(const AString& str, usize* pos = nullptr){ return std::stof(str, pos); }
[[nodiscard]] inline f64 Stod(const AString& str, usize* pos = nullptr){ return std::stod(str, pos); }


template<typename CharT = char, typename PathT>
[[nodiscard]] inline BasicString<CharT> PathToString(const PathT& path){
    if constexpr(SameAs<CharT, char>)
        return path.generic_string();
    else
        return path.generic_wstring();
}


template<typename CharT>
[[nodiscard]] inline BasicString<CharT> BuildSafeCacheName(const BasicStringView<CharT> text){
    BasicString<CharT> output;
    output.reserve(text.size());

    for(const CharT ch : text){
        const bool alphaNum = (ch >= CharT('a') && ch <= CharT('z'))
            || (ch >= CharT('0') && ch <= CharT('9'));
        const bool safePunctuation = ch == CharT('.') || ch == CharT('_') || ch == CharT('-');

        if(alphaNum || safePunctuation)
            output.push_back(ch);
        else
            output.push_back(CharT('_'));
    }

    return output;
}
template<typename CharT>
[[nodiscard]] inline BasicString<CharT> BuildSafeCacheName(const BasicString<CharT>& text){
    return BuildSafeCacheName<CharT>(BasicStringView<CharT>{text});
}


template<typename CharT>
inline constexpr CharT Canonicalize(CharT c){
    if(c == static_cast<CharT>('\\'))
        return static_cast<CharT>('/');
    return (c >= static_cast<CharT>('A') && c <= static_cast<CharT>('Z'))
        ? static_cast<CharT>(c + (static_cast<CharT>('a') - static_cast<CharT>('A')))
        : c
    ;
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

template<typename CharT>
[[nodiscard]] inline BasicString<CharT> CanonicalizeText(const BasicStringView<CharT> text){
    BasicString<CharT> output;
    output.reserve(text.size());
    for(const CharT ch : text)
        output.push_back(Canonicalize(ch));

    return output;
}
template<typename CharT>
[[nodiscard]] inline BasicString<CharT> CanonicalizeText(const BasicString<CharT>& text){
    return CanonicalizeText<CharT>(BasicStringView<CharT>{text});
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
[[nodiscard]] inline bool HasEmbeddedNull(const BasicString<CharT>& text){
    return HasEmbeddedNull(BasicStringView<CharT>(text));
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

