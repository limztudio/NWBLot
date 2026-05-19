// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"

#include <cctype>
#include <charconv>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline bool IsAsciiSpace(const CharT ch){
    return
        ch == CharT(' ') || ch == CharT('\t') || ch == CharT('\n')
        || ch == CharT('\r') || ch == CharT('\f') || ch == CharT('\v')
    ;
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


template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicString<CharT, ArenaT> Trim(ArenaT& arena, const BasicStringView<CharT> text){
    return BasicString<CharT, ArenaT>(TrimView(text), arena);
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicString<CharT, ArenaT> Trim(const BasicString<CharT, ArenaT>& text){
    return BasicString<CharT, ArenaT>(TrimView<CharT>(BasicStringView<CharT>{text}), text.get_allocator());
}


template<typename CharT, typename ArenaT>
inline void TrimTrailingCarriageReturn(BasicString<CharT, ArenaT>& inOutLine){
    if(!inOutLine.empty() && inOutLine.back() == CharT('\r'))
        inOutLine.pop_back();
}


template<typename StringT>
inline void StripUtf8Bom(StringT& inOutText){
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


template<typename CharT, typename ArenaT>
[[nodiscard]] inline bool ReadTextLine(BasicStringStream<CharT, ArenaT>& stream, BasicString<CharT, ArenaT>& outLine){
    return static_cast<bool>(std::getline(stream, outLine));
}
template<typename CharT, typename Traits, typename ArenaT>
[[nodiscard]] inline bool ReadTextLine(std::basic_istream<CharT, Traits>& stream, BasicString<CharT, ArenaT>& outLine){
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
    if constexpr(SameAs<CharT, char>)
        return BasicString<CharT, ArenaT>(path.generic_string(), arena);
    else
        return BasicString<CharT, ArenaT>(path.generic_wstring(), arena);
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
    return
        (c >= static_cast<CharT>('A') && c <= static_cast<CharT>('Z'))
            ? static_cast<CharT>(c + (static_cast<CharT>('a') - static_cast<CharT>('A')))
            : c
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

template<typename CharT, typename ArenaT, bool Canonical>
[[nodiscard]] inline BasicString<CharT, ArenaT> BuildSafeCacheNameImpl(ArenaT& arena, const BasicStringView<CharT> text){
    if(text.empty())
        return BasicString<CharT, ArenaT>(arena);

    BasicString<CharT, ArenaT> output(text.data(), text.size(), arena);
    for(CharT& ch : output){
        if constexpr(Canonical)
            ch = Canonicalize(ch);

        if(!IsSafeCacheNameChar(ch))
            ch = CharT('_');
    }

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
    if(text.empty())
        return BasicString<CharT, ArenaT>(text.get_allocator());

    BasicString<CharT, ArenaT> output(text.data(), text.size(), text.get_allocator());
    for(CharT& ch : output){
        if(!TextUtilsDetail::IsSafeCacheNameChar(ch))
            ch = CharT('_');
    }
    return output;
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
    using CharT = typename StringT::value_type;
    if(text.empty())
        return StringT(text.get_allocator());

    StringT output(text.data(), text.size(), text.get_allocator());
    for(CharT& ch : output){
        if(!TextUtilsDetail::IsSafeCacheNameChar(ch))
            ch = CharT('_');
    }
    return output;
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
    if(text.empty())
        return BasicString<CharT, ArenaT>(text.get_allocator());

    BasicString<CharT, ArenaT> output(text.data(), text.size(), text.get_allocator());
    for(CharT& ch : output){
        ch = Canonicalize(ch);
        if(!TextUtilsDetail::IsSafeCacheNameChar(ch))
            ch = CharT('_');
    }
    return output;
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
    using CharT = typename StringT::value_type;
    if(text.empty())
        return StringT(text.get_allocator());

    StringT output(text.data(), text.size(), text.get_allocator());
    for(CharT& ch : output){
        ch = Canonicalize(ch);
        if(!TextUtilsDetail::IsSafeCacheNameChar(ch))
            ch = CharT('_');
    }
    return output;
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

