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

template <typename T>
using BasicString = std::basic_string<T>;
using AString = BasicString<char>;
using WString = BasicString<wchar>;
using TString = BasicString<tchar>;

template <typename T>
using BasicStringStream = std::basic_stringstream<T>;
using AStringStream = BasicStringStream<char>;
using WStringStream = BasicStringStream<wchar>;
using TStringStream = BasicStringStream<tchar>;

using AFormatContext = std::format_context;
using WFormatContext = std::wformat_context;
template <typename T>
using BasicFormatContext = Conditional_T<SameAs<char, T>, AFormatContext, Conditional_T<SameAs<wchar, T>, WFormatContext, void>>;
using TStringFormatContext = BasicFormatContext<tchar>;

template <typename T>
using BasicFormatArgs = std::basic_format_args<BasicFormatContext<T>>;
using AFormatArgs = BasicFormatArgs<char>;
using WFormatArgs = BasicFormatArgs<wchar>;
using TStringFormatArgs = BasicFormatArgs<tchar>;

template <typename... T>
using AFormatString = std::format_string<T...>;
template <typename... T>
using WFormatString = std::wformat_string<T...>;

template <usize N>
struct ConstString{
    char data[N];
    constexpr ConstString(const char(&str)[N]){
        for(usize i = 0; i < N; ++i)
            data[i] = str[i];
    }
    constexpr operator const char*()const{ return data; }
    constexpr const char* c_str()const{ return data; }
};
template <usize N>
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
template <usize N>
using ConstTString = ConstWString<N>;
#else
template <usize N>
using ConstTString = ConstString<N>;
#endif

template <usize N>
constexpr auto MakeConstString(const char(&str)[N]){ return ConstString<N>(str); }
template <usize N>
constexpr auto MakeConstWString(const wchar(&str)[N]){ return ConstWString<N>(str); }
#if defined(NWB_UNICODE)
#define MakeConstTString MakeConstWString
#else
#define MakeConstTString MakeConstString
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_basic_string{
    template<typename In>
    concept FromWcharView = IsConvertible_V<In, WStringView>;

    template<typename In>
    concept FromCharView = IsConvertible_V<In, AStringView>;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename In> requires __hidden_basic_string::FromWcharView<In>
inline AString StringConvert(const In& raw){
    WStringView src(raw);
    if(src.empty())
        return AString();
#if defined(NWB_PLATFORM_WINDOWS)
    const auto len = WideCharToMultiByte(CP_ACP, 0, src.data(), static_cast<int>(src.length()), nullptr, 0, nullptr, nullptr);
    NWB_ASSERT(len != 0);
    AString dst(len, 0);
    WideCharToMultiByte(CP_ACP, 0, src.data(), static_cast<int>(src.length()), dst.data(), len, nullptr, nullptr);
    return dst;
#endif
}
template<typename In> requires __hidden_basic_string::FromWcharView<In>
inline AString StringConvert(In&& raw){
    WStringView src(Move(raw));
    if(src.empty())
        return AString();
#if defined(NWB_PLATFORM_WINDOWS)
    const auto len = WideCharToMultiByte(CP_ACP, 0, src.data(), static_cast<int>(src.length()), nullptr, 0, nullptr, nullptr);
    NWB_ASSERT(len != 0);
    AString dst(len, 0);
    WideCharToMultiByte(CP_ACP, 0, src.data(), static_cast<int>(src.length()), dst.data(), len, nullptr, nullptr);
    return dst;
#endif
}
template<typename In>
inline AString StringConvert(const In& src){ return src; }
template<typename In>
inline AString StringConvert(In&& src){ return src; }

template<typename In> requires __hidden_basic_string::FromCharView<In>
inline WString StringConvert(const In& raw){
    AStringView src(raw);
    if(src.empty())
        return WString();
#if defined(NWB_PLATFORM_WINDOWS)
    const auto len = MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), nullptr, 0);
    NWB_ASSERT(len != 0);
    WString dst(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), dst.data(), len);
    return dst;
#endif
}
template<typename In> requires __hidden_basic_string::FromCharView<In>
inline WString StringConvert(In&& raw){
    AStringView src(Move(raw));
    if(src.empty())
        return WString();
#if defined(NWB_PLATFORM_WINDOWS)
    const auto len = MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), nullptr, 0);
    NWB_ASSERT(len != 0);
    WString dst(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), dst.data(), len);
    return dst;
#endif
}
template<typename In>
inline WString StringConvert(const In& src){ return src; }
template<typename In>
inline WString StringConvert(In&& src){ return src; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename... T>
inline AString StringFormat(AFormatString<T...> fmt, T&&... args){
    return std::vformat(fmt.get(), std::make_format_args<AFormatContext>(args...));
}
template <typename... T>
inline WString StringFormat(WFormatString<T...> fmt, T&&... args){
    return std::vformat(fmt.get(), std::make_format_args<WFormatContext>(args...));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

