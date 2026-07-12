
#pragma once


#include <string_view>
#include <string>
#include <sstream>
#include <format>
#include <iterator>

#include "assert.h"
#include "generic.h"
#include "type.h"
#include "container/adaptor.h"

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using BasicStringView = std::basic_string_view<T>;
using AStringView = BasicStringView<char>;
using WStringView = BasicStringView<wchar>;
using TStringView = BasicStringView<tchar>;

template<typename T, typename ArenaT>
using BasicString = std::basic_string<T, std::char_traits<T>, ContainerDetail::ArenaAllocatorFor_T<T, ArenaT>>;
template<typename ArenaT>
using AString = BasicString<char, ArenaT>;
template<typename ArenaT>
using WString = BasicString<wchar, ArenaT>;
template<typename ArenaT>
using TString = BasicString<tchar, ArenaT>;

template<typename T, typename ArenaT>
using BasicStringStream = std::basic_stringstream<T, std::char_traits<T>, ContainerDetail::ArenaAllocatorFor_T<T, ArenaT>>;
template<typename ArenaT>
using AStringStream = BasicStringStream<char, ArenaT>;
template<typename ArenaT>
using WStringStream = BasicStringStream<wchar, ArenaT>;
template<typename ArenaT>
using TStringStream = BasicStringStream<tchar, ArenaT>;

using AFormatContext = std::format_context;
using WFormatContext = std::wformat_context;
template<typename T>
using BasicFormatContext = Conditional_T<SameAs<char, T>, AFormatContext, Conditional_T<SameAs<wchar, T>, WFormatContext, void>>;
using TStringFormatContext = BasicFormatContext<tchar>;

template<typename T>
using BasicFormatArgs = std::basic_format_args<BasicFormatContext<T>>;
using AFormatArgs = BasicFormatArgs<char>;
using WFormatArgs = BasicFormatArgs<wchar>;
using TStringFormatArgs = BasicFormatArgs<tchar>;

template<typename... T>
using AFormatString = std::format_string<T...>;
template<typename... T>
using WFormatString = std::wformat_string<T...>;

template<usize N>
struct ConstString{
    char data[N];
    constexpr ConstString(const char(&str)[N]){
        for(usize i = 0; i < N; ++i)
            data[i] = str[i];
    }
    constexpr operator const char*()const{ return data; }
    constexpr const char* c_str()const{ return data; }
};
template<usize N>
struct ConstWString{
    wchar data[N];
    constexpr ConstWString(const wchar(&str)[N]){
        for(usize i = 0; i < N; ++i)
            data[i] = str[i];
    }
    constexpr operator const wchar*()const{ return data; }
    constexpr const wchar* c_str()const{ return data; }
};
#if defined(NWB_UNICODE)
template<usize N>
using ConstTString = ConstWString<N>;
#else
template<usize N>
using ConstTString = ConstString<N>;
#endif

template<usize N>
constexpr auto MakeConstString(const char(&str)[N]){ return ConstString<N>(str); }
template<usize N>
constexpr auto MakeConstWString(const wchar(&str)[N]){ return ConstWString<N>(str); }
#if defined(NWB_UNICODE)
#define MakeConstTString MakeConstWString
#else
#define MakeConstTString MakeConstString
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace BasicStringDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename In>
concept FromWcharView = IsConvertible_V<In, WStringView>;

template<typename In>
concept FromCharView = IsConvertible_V<In, AStringView>;

template<typename In>
concept CanMakeCharView = requires(const In& input){ AStringView(input); };

template<typename In>
concept CanMakeWCharView = requires(const In& input){ WStringView(input); };

inline constexpr u8 s_Utf8ContinuationMask = 0xC0u;
inline constexpr u8 s_Utf8ContinuationMarker = 0x80u;
inline constexpr u8 s_Utf8OneByteMaxExclusive = 0x80u;
inline constexpr u8 s_Utf8TwoByteMask = 0xE0u;
inline constexpr u8 s_Utf8TwoByteMarker = 0xC0u;
inline constexpr u8 s_Utf8ThreeByteMask = 0xF0u;
inline constexpr u8 s_Utf8ThreeByteMarker = 0xE0u;
inline constexpr u8 s_Utf8FourByteMask = 0xF8u;
inline constexpr u8 s_Utf8FourByteMarker = 0xF0u;
inline constexpr u8 s_Utf8TwoBytePayloadMask = 0x1Fu;
inline constexpr u8 s_Utf8ThreeBytePayloadMask = 0x0Fu;
inline constexpr u8 s_Utf8FourBytePayloadMask = 0x07u;
inline constexpr u8 s_Utf8ContinuationPayloadMask = 0x3Fu;
inline constexpr u32 s_Utf8ContinuationPayloadBits = 6u;
inline constexpr u32 s_Utf8OneByteMaxCodePoint = 0x7Fu;
inline constexpr u32 s_Utf8TwoByteMaxCodePoint = 0x7FFu;
inline constexpr u32 s_Utf8ThreeByteMaxCodePoint = 0xFFFFu;
inline constexpr u32 s_Utf8TwoByteMinCodePoint = 0x80u;
inline constexpr u32 s_Utf8ThreeByteMinCodePoint = 0x800u;
inline constexpr u32 s_Utf8FourByteMinCodePoint = 0x10000u;
inline constexpr u32 s_UnicodeMaxCodePoint = 0x10FFFFu;
inline constexpr u32 s_UnicodeHighSurrogateMin = 0xD800u;
inline constexpr u32 s_UnicodeHighSurrogateMax = 0xDBFFu;
inline constexpr u32 s_UnicodeLowSurrogateMin = 0xDC00u;
inline constexpr u32 s_UnicodeLowSurrogateMax = 0xDFFFu;
inline constexpr u32 s_Utf16SurrogatePayloadBits = 10u;
inline constexpr u32 s_Utf16SurrogatePayloadMask = 0x3FFu;
inline constexpr u32 s_InvalidCodePointReplacement = static_cast<u32>('?');

template<typename In>
struct StringConvertArg{
    const In& value;
};

template<typename Out>
inline void WriteUtf8CodePoint(Out& out, u32 codePoint){
    if(codePoint <= s_Utf8OneByteMaxCodePoint){
        *out++ = static_cast<char>(codePoint);
        return;
    }

    if(codePoint <= s_Utf8TwoByteMaxCodePoint){
        *out++ = static_cast<char>(s_Utf8TwoByteMarker | (codePoint >> s_Utf8ContinuationPayloadBits));
        *out++ = static_cast<char>(s_Utf8ContinuationMarker | (codePoint & s_Utf8ContinuationPayloadMask));
        return;
    }

    if(codePoint <= s_Utf8ThreeByteMaxCodePoint){
        *out++ = static_cast<char>(s_Utf8ThreeByteMarker | (codePoint >> (s_Utf8ContinuationPayloadBits * 2u)));
        *out++ = static_cast<char>(s_Utf8ContinuationMarker | ((codePoint >> s_Utf8ContinuationPayloadBits) & s_Utf8ContinuationPayloadMask));
        *out++ = static_cast<char>(s_Utf8ContinuationMarker | (codePoint & s_Utf8ContinuationPayloadMask));
        return;
    }

    if(codePoint <= s_UnicodeMaxCodePoint){
        *out++ = static_cast<char>(s_Utf8FourByteMarker | (codePoint >> (s_Utf8ContinuationPayloadBits * 3u)));
        *out++ = static_cast<char>(s_Utf8ContinuationMarker | ((codePoint >> (s_Utf8ContinuationPayloadBits * 2u)) & s_Utf8ContinuationPayloadMask));
        *out++ = static_cast<char>(s_Utf8ContinuationMarker | ((codePoint >> s_Utf8ContinuationPayloadBits) & s_Utf8ContinuationPayloadMask));
        *out++ = static_cast<char>(s_Utf8ContinuationMarker | (codePoint & s_Utf8ContinuationPayloadMask));
        return;
    }

    *out++ = static_cast<char>(s_InvalidCodePointReplacement);
}

template<typename Out>
inline void WriteWideCodePoint(Out& out, u32 codePoint){
    if(codePoint > s_UnicodeMaxCodePoint)
        codePoint = s_InvalidCodePointReplacement;

#if WCHAR_MAX <= 0xFFFF
    if(codePoint > s_Utf8ThreeByteMaxCodePoint){
        codePoint -= s_Utf8FourByteMinCodePoint;
        *out++ = static_cast<wchar>(s_UnicodeHighSurrogateMin + (codePoint >> s_Utf16SurrogatePayloadBits));
        *out++ = static_cast<wchar>(s_UnicodeLowSurrogateMin + (codePoint & s_Utf16SurrogatePayloadMask));
        return;
    }
#endif

    *out++ = static_cast<wchar>(codePoint);
}

template<typename Out>
inline void WriteWStringAsUtf8(Out& out, const WStringView src){
    for(usize i = 0; i < src.size(); ++i){
        u32 codePoint = static_cast<u32>(src[i]);

#if WCHAR_MAX <= 0xFFFF
        if(codePoint >= s_UnicodeHighSurrogateMin && codePoint <= s_UnicodeHighSurrogateMax && (i + 1) < src.size()){
            const u32 low = static_cast<u32>(src[i + 1]);
            if(low >= s_UnicodeLowSurrogateMin && low <= s_UnicodeLowSurrogateMax){
                codePoint = s_Utf8FourByteMinCodePoint
                    + (((codePoint - s_UnicodeHighSurrogateMin) << s_Utf16SurrogatePayloadBits) | (low - s_UnicodeLowSurrogateMin));
                ++i;
            }
            else
                codePoint = s_InvalidCodePointReplacement;
        }
        else if(codePoint >= s_UnicodeHighSurrogateMin && codePoint <= s_UnicodeLowSurrogateMax){
            codePoint = s_InvalidCodePointReplacement;
        }
#endif

        WriteUtf8CodePoint(out, codePoint);
    }
}

template<typename ArenaT>
[[nodiscard]] inline AString<ArenaT> WideToUtf8(ArenaT& arena, WStringView src){
    AString<ArenaT> dst{arena};
    if(src.empty())
        return dst;

    dst.reserve(src.size());
    auto out = std::back_inserter(dst);
    WriteWStringAsUtf8(out, src);
    return dst;
}

template<typename Out>
inline void WriteUtf8AsWString(Out& out, const AStringView src){
    usize i = 0u;
    while(i < src.size()){
        const u8 first = static_cast<u8>(src[i++]);
        u32 codePoint = s_InvalidCodePointReplacement;

        if(first < s_Utf8OneByteMaxExclusive){
            codePoint = first;
        }
        else if((first & s_Utf8TwoByteMask) == s_Utf8TwoByteMarker && i < src.size()){
            const u8 second = static_cast<u8>(src[i]);
            if((second & s_Utf8ContinuationMask) == s_Utf8ContinuationMarker){
                ++i;
                codePoint = (static_cast<u32>(first & s_Utf8TwoBytePayloadMask) << s_Utf8ContinuationPayloadBits)
                    | static_cast<u32>(second & s_Utf8ContinuationPayloadMask);
            }
        }
        else if((first & s_Utf8ThreeByteMask) == s_Utf8ThreeByteMarker && (i + 1u) < src.size()){
            const u8 second = static_cast<u8>(src[i]);
            const u8 third = static_cast<u8>(src[i + 1u]);
            if((second & s_Utf8ContinuationMask) == s_Utf8ContinuationMarker && (third & s_Utf8ContinuationMask) == s_Utf8ContinuationMarker){
                i += 2u;
                codePoint =
                    (static_cast<u32>(first & s_Utf8ThreeBytePayloadMask) << (s_Utf8ContinuationPayloadBits * 2u))
                    | (static_cast<u32>(second & s_Utf8ContinuationPayloadMask) << s_Utf8ContinuationPayloadBits)
                    | static_cast<u32>(third & s_Utf8ContinuationPayloadMask)
                ;
            }
        }
        else if((first & s_Utf8FourByteMask) == s_Utf8FourByteMarker && (i + 2u) < src.size()){
            const u8 second = static_cast<u8>(src[i]);
            const u8 third = static_cast<u8>(src[i + 1u]);
            const u8 fourth = static_cast<u8>(src[i + 2u]);
            if(
                (second & s_Utf8ContinuationMask) == s_Utf8ContinuationMarker
                && (third & s_Utf8ContinuationMask) == s_Utf8ContinuationMarker
                && (fourth & s_Utf8ContinuationMask) == s_Utf8ContinuationMarker
            ){
                i += 3u;
                codePoint =
                    (static_cast<u32>(first & s_Utf8FourBytePayloadMask) << (s_Utf8ContinuationPayloadBits * 3u))
                    | (static_cast<u32>(second & s_Utf8ContinuationPayloadMask) << (s_Utf8ContinuationPayloadBits * 2u))
                    | (static_cast<u32>(third & s_Utf8ContinuationPayloadMask) << s_Utf8ContinuationPayloadBits)
                    | static_cast<u32>(fourth & s_Utf8ContinuationPayloadMask)
                ;
            }
        }

        WriteWideCodePoint(out, codePoint);
    }
}

template<typename CharT, typename Out, typename In>
inline void WriteConvertedText(Out& out, const In& input){
    if constexpr(CanMakeCharView<In>){
        const AStringView text(input);
        if constexpr(IsSame_V<CharT, char>){
            for(const char ch : text)
                *out++ = ch;
        }
        else{
            WriteUtf8AsWString(out, text);
        }
    }
    else if constexpr(CanMakeWCharView<In>){
        const WStringView text(input);
        if constexpr(IsSame_V<CharT, wchar>){
            for(const wchar ch : text)
                *out++ = ch;
        }
        else{
            WriteWStringAsUtf8(out, text);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_UNICODE)
template<typename ArenaT, typename In> requires BasicStringDetail::FromCharView<In>
inline TString<ArenaT> StringConvert(ArenaT& arena, const In& raw){
    AStringView src(raw);
    TString<ArenaT> dst{arena};
    if(src.empty())
        return dst;

#if defined(NWB_PLATFORM_WINDOWS)
    const auto len = MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), nullptr, 0);
    NWB_ASSERT(len != 0);
    dst.resize(len);
    MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), dst.data(), len);
    return dst;
#else
    dst.resize(src.size());
    for(usize i = 0u; i < src.size(); ++i)
        dst[i] = static_cast<wchar>(static_cast<unsigned char>(src[i]));
    return dst;
#endif
}

template<typename ArenaT, typename In>
inline TString<ArenaT> StringConvert(ArenaT& arena, const In& src){
    return TString<ArenaT>(src, arena);
}
#else
template<typename ArenaT, typename In> requires BasicStringDetail::FromWcharView<In>
inline TString<ArenaT> StringConvert(ArenaT& arena, const In& raw){
    const WStringView src(raw);
    if(src.empty())
        return TString<ArenaT>{arena};

#if defined(NWB_PLATFORM_WINDOWS)
    const auto len = WideCharToMultiByte(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), nullptr, 0, nullptr, nullptr);
    NWB_ASSERT(len != 0);
    TString<ArenaT> dst{arena};
    dst.resize(len);
    WideCharToMultiByte(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), dst.data(), len, nullptr, nullptr);
    return dst;
#else
    return BasicStringDetail::WideToUtf8(arena, src);
#endif
}

template<typename ArenaT, typename In>
inline TString<ArenaT> StringConvert(ArenaT& arena, const In& src){
    return TString<ArenaT>(src, arena);
}
#endif

template<typename In>
inline BasicStringDetail::StringConvertArg<In> StringConvert(const In& src){
    return BasicStringDetail::StringConvertArg<In>{ src };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT, typename... T>
inline AString<ArenaT> StringFormat(ArenaT& arena, AFormatString<T...> fmt, T&&... args){
    AString<ArenaT> output{arena};
    std::vformat_to(std::back_inserter(output), fmt.get(), std::make_format_args<AFormatContext>(args...));
    return output;
}
template<typename ArenaT, typename... T>
inline WString<ArenaT> StringFormat(ArenaT& arena, WFormatString<T...> fmt, T&&... args){
    WString<ArenaT> output{arena};
    std::vformat_to(std::back_inserter(output), fmt.get(), std::make_format_args<WFormatContext>(args...));
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename In, typename CharT>
struct formatter<BasicStringDetail::StringConvertArg<In>, CharT>{
    constexpr auto parse(basic_format_parse_context<CharT>& ctx){
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(const BasicStringDetail::StringConvertArg<In>& arg, FormatContext& ctx)const{
        auto out = ctx.out();
        BasicStringDetail::WriteConvertedText<CharT>(out, arg.value);
        return out;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

