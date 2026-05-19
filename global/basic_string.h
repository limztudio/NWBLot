// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

template<typename In>
struct StringConvertArg{
    const In& value;
};

template<typename StringT>
inline void AppendUtf8CodePoint(StringT& out, u32 codePoint){
    if(codePoint <= 0x7F){
        out.push_back(static_cast<char>(codePoint));
        return;
    }

    if(codePoint <= 0x7FF){
        out.push_back(static_cast<char>(0xC0u | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
        return;
    }

    if(codePoint <= 0xFFFF){
        out.push_back(static_cast<char>(0xE0u | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
        return;
    }

    if(codePoint <= 0x10FFFF){
        out.push_back(static_cast<char>(0xF0u | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
        return;
    }

    out.push_back('?');
}

template<typename ArenaT>
[[nodiscard]] inline AString<ArenaT> WideToUtf8(ArenaT& arena, WStringView src){
    AString<ArenaT> dst{arena};
    if(src.empty())
        return dst;

    dst.reserve(src.size());

    for(usize i = 0; i < src.size(); ++i){
        u32 codePoint = static_cast<u32>(src[i]);

#if WCHAR_MAX <= 0xFFFF
        if(codePoint >= 0xD800u && codePoint <= 0xDBFFu && (i + 1) < src.size()){
            const u32 low = static_cast<u32>(src[i + 1]);
            if(low >= 0xDC00u && low <= 0xDFFFu){
                codePoint = 0x10000u + (((codePoint - 0xD800u) << 10) | (low - 0xDC00u));
                ++i;
            }
            else
                codePoint = static_cast<u32>('?');
        }
        else if(codePoint >= 0xD800u && codePoint <= 0xDBFFu){
            codePoint = static_cast<u32>('?');
        }
        else if(codePoint >= 0xDC00u && codePoint <= 0xDFFFu){
            codePoint = static_cast<u32>('?');
        }
#endif

        AppendUtf8CodePoint(dst, codePoint);
    }

    return dst;
}

template<typename Out>
inline void WriteUtf8CodePoint(Out& out, u32 codePoint){
    if(codePoint <= 0x7F){
        *out++ = static_cast<char>(codePoint);
        return;
    }

    if(codePoint <= 0x7FF){
        *out++ = static_cast<char>(0xC0u | (codePoint >> 6));
        *out++ = static_cast<char>(0x80u | (codePoint & 0x3Fu));
        return;
    }

    if(codePoint <= 0xFFFF){
        *out++ = static_cast<char>(0xE0u | (codePoint >> 12));
        *out++ = static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu));
        *out++ = static_cast<char>(0x80u | (codePoint & 0x3Fu));
        return;
    }

    if(codePoint <= 0x10FFFF){
        *out++ = static_cast<char>(0xF0u | (codePoint >> 18));
        *out++ = static_cast<char>(0x80u | ((codePoint >> 12) & 0x3Fu));
        *out++ = static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu));
        *out++ = static_cast<char>(0x80u | (codePoint & 0x3Fu));
        return;
    }

    *out++ = '?';
}

template<typename Out>
inline void WriteWideCodePoint(Out& out, u32 codePoint){
    if(codePoint > 0x10FFFF)
        codePoint = static_cast<u32>('?');

#if WCHAR_MAX <= 0xFFFF
    if(codePoint > 0xFFFF){
        codePoint -= 0x10000u;
        *out++ = static_cast<wchar>(0xD800u + (codePoint >> 10));
        *out++ = static_cast<wchar>(0xDC00u + (codePoint & 0x3FFu));
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
        if(codePoint >= 0xD800u && codePoint <= 0xDBFFu && (i + 1) < src.size()){
            const u32 low = static_cast<u32>(src[i + 1]);
            if(low >= 0xDC00u && low <= 0xDFFFu){
                codePoint = 0x10000u + (((codePoint - 0xD800u) << 10) | (low - 0xDC00u));
                ++i;
            }
            else
                codePoint = static_cast<u32>('?');
        }
        else if(codePoint >= 0xD800u && codePoint <= 0xDFFFu){
            codePoint = static_cast<u32>('?');
        }
#endif

        WriteUtf8CodePoint(out, codePoint);
    }
}

template<typename Out>
inline void WriteUtf8AsWString(Out& out, const AStringView src){
    usize i = 0u;
    while(i < src.size()){
        const u8 first = static_cast<u8>(src[i++]);
        u32 codePoint = static_cast<u32>('?');

        if(first < 0x80u){
            codePoint = first;
        }
        else if((first & 0xE0u) == 0xC0u && i < src.size()){
            const u8 second = static_cast<u8>(src[i]);
            if((second & 0xC0u) == 0x80u){
                ++i;
                codePoint = (static_cast<u32>(first & 0x1Fu) << 6) | static_cast<u32>(second & 0x3Fu);
            }
        }
        else if((first & 0xF0u) == 0xE0u && (i + 1u) < src.size()){
            const u8 second = static_cast<u8>(src[i]);
            const u8 third = static_cast<u8>(src[i + 1u]);
            if((second & 0xC0u) == 0x80u && (third & 0xC0u) == 0x80u){
                i += 2u;
                codePoint =
                    (static_cast<u32>(first & 0x0Fu) << 12)
                    | (static_cast<u32>(second & 0x3Fu) << 6)
                    | static_cast<u32>(third & 0x3Fu)
                ;
            }
        }
        else if((first & 0xF8u) == 0xF0u && (i + 2u) < src.size()){
            const u8 second = static_cast<u8>(src[i]);
            const u8 third = static_cast<u8>(src[i + 1u]);
            const u8 fourth = static_cast<u8>(src[i + 2u]);
            if((second & 0xC0u) == 0x80u && (third & 0xC0u) == 0x80u && (fourth & 0xC0u) == 0x80u){
                i += 3u;
                codePoint =
                    (static_cast<u32>(first & 0x07u) << 18)
                    | (static_cast<u32>(second & 0x3Fu) << 12)
                    | (static_cast<u32>(third & 0x3Fu) << 6)
                    | static_cast<u32>(fourth & 0x3Fu)
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

