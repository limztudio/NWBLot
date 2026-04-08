// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <string_view>
#include <string>
#include <sstream>
#include <format>

#include "generic.h"
#include "type.h"

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using BasicStringView = std::basic_string_view<T>;
using AStringView = BasicStringView<char>;
using WStringView = BasicStringView<wchar>;
using TStringView = BasicStringView<tchar>;

template<typename T, typename Allocator = std::allocator<T>>
using BasicString = std::basic_string<T, std::char_traits<T>, Allocator>;
using AString = BasicString<char>;
using WString = BasicString<wchar>;
using TString = BasicString<tchar>;

template<typename T, typename Allocator = std::allocator<T>>
using BasicStringStream = std::basic_stringstream<T, std::char_traits<T>, Allocator>;
using AStringStream = BasicStringStream<char>;
using WStringStream = BasicStringStream<wchar>;
using TStringStream = BasicStringStream<tchar>;

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


namespace __hidden_basic_string{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename In>
concept FromWcharView = IsConvertible_V<In, WStringView>;

template<typename In>
concept FromCharView = IsConvertible_V<In, AStringView>;

inline void AppendUtf8CodePoint(AString& out, u32 codePoint){
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

[[nodiscard]] inline AString WideToUtf8(WStringView src){
    if(src.empty())
        return AString();

    AString dst;
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
        }else if(codePoint >= 0xDC00u && codePoint <= 0xDFFFu){
            codePoint = static_cast<u32>('?');
        }
#endif

        AppendUtf8CodePoint(dst, codePoint);
    }

    return dst;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_UNICODE)
template<typename In> requires __hidden_basic_string::FromCharView<In>
inline TString StringConvert(const In& raw){
    AStringView src(raw);
    if(src.empty())
        return TString();

#if defined(NWB_PLATFORM_WINDOWS)
    const auto len = MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), nullptr, 0);
    NWB_ASSERT(len != 0);
    TString dst(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), dst.data(), len);
    return dst;
#else
    TString dst;
    dst.reserve(src.size());
    for(unsigned char ch : src)
        dst.push_back(static_cast<wchar>(ch));
    return dst;
#endif
}

template<typename In>
inline TString StringConvert(const In& src){ return TString(src); }
#else
template<typename In> requires __hidden_basic_string::FromWcharView<In>
inline TString StringConvert(const In& raw){
    const WStringView src(raw);
    if(src.empty())
        return TString();

#if defined(NWB_PLATFORM_WINDOWS)
    const auto len = WideCharToMultiByte(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), nullptr, 0, nullptr, nullptr);
    NWB_ASSERT(len != 0);
    TString dst(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), dst.data(), len, nullptr, nullptr);
    return dst;
#else
    return __hidden_basic_string::WideToUtf8(src);
#endif
}

template<typename In>
inline TString StringConvert(const In& src){ return TString(src); }
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename... T>
inline AString StringFormat(AFormatString<T...> fmt, T&&... args){
    return std::vformat(fmt.get(), std::make_format_args<AFormatContext>(args...));
}
template<typename... T>
inline WString StringFormat(WFormatString<T...> fmt, T&&... args){
    return std::vformat(fmt.get(), std::make_format_args<WFormatContext>(args...));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
