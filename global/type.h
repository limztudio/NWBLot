// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <type_traits>

#include <string>
#include <string_view>

#include <cassert>

#include "compile.h"
#include "platform.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(UNICODE) || defined(_UNICODE)
#define NWB_UNICODE
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


typedef char i8;
typedef unsigned char u8;

typedef short i16;
typedef unsigned short u16;

typedef long i32;
typedef unsigned long u32;

typedef long long i64;
typedef unsigned long long u64;

using isize = std::conditional_t<sizeof(void*) == 8, __int64, int>;
using usize = std::conditional_t<sizeof(void*) == 8, unsigned __int64, unsigned int>;

typedef float f32;
typedef double f64;

//typedef char char;
typedef wchar_t wchar;
#ifdef NWB_UNICODE
typedef wchar tchar;
#else
typedef char tchar;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_UNICODE
#define __NWB_TEXT(x) L ## x
#else
#define __NWB_TEXT(x) x
#endif
#define NWB_TEXT(x) __NWB_TEXT(x)

#if defined(NWB_PLATFORM_WINDOWS)
#if defined(_MSC_VER)
#define NWB_DLL_EXPORT __declspec(dllexport)
#define NWB_DLL_IMPORT __declspec(dllimport)
#else
#define NWB_DLL_EXPORT __attribute__((dllexport))
#define NWB_DLL_IMPORT __attribute__((dllimport))
#endif
#elif (defined(NWB_PLATFORM_UNIX) || defined(NWB_PLATFORM_APPLE))
#define NWB_DLL_EXPORT __attribute__((visibility("default")))
#define NWB_DLL_IMPORT
#else
#define NWB_DLL_EXPORT
#define NWB_DLL_IMPORT
#endif

#if defined(NWB_EXPORT_DLL)
#define NWB_DLL_API NWB_DLL_EXPORT
#else
#define NWB_DLL_API NWB_DLL_IMPORT
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace __hidden_type_convert{
    template<typename In>
    concept FromWcharView = std::is_convertible_v<In, std::basic_string_view<wchar>>;

    template<typename In>
    concept FromCharView = std::is_convertible_v<In, std::basic_string_view<char>>;
};

template<typename In> requires __hidden_type_convert::FromWcharView<In>
inline std::basic_string<char> convert(const In& raw){
    std::basic_string_view<wchar> src(raw);
    if(src.empty())
        return std::basic_string<char>();
#ifdef NWB_PLATFORM_WINDOWS
    const auto len = WideCharToMultiByte(CP_ACP, 0, src.data(), static_cast<int>(src.length()), nullptr, 0, nullptr, nullptr);
    assert(len != 0);
    std::basic_string<char> dst(len, 0);
    WideCharToMultiByte(CP_ACP, 0, src.data(), static_cast<int>(src.length()), dst.data(), len, nullptr, nullptr);
    return dst;
#endif
}
template<typename In> requires __hidden_type_convert::FromWcharView<In>
inline std::basic_string<char> convert(In&& raw){
    std::basic_string_view<wchar> src(std::move(raw));
    if(src.empty())
        return std::basic_string<char>();
#ifdef NWB_PLATFORM_WINDOWS
    const auto len = WideCharToMultiByte(CP_ACP, 0, src.data(), static_cast<int>(src.length()), nullptr, 0, nullptr, nullptr);
    assert(len != 0);
    std::basic_string<char> dst(len, 0);
    WideCharToMultiByte(CP_ACP, 0, src.data(), static_cast<int>(src.length()), dst.data(), len, nullptr, nullptr);
    return dst;
#endif
}
template<typename In>
inline std::basic_string<char> convert(const In& src){ return src; }
template<typename In>
inline std::basic_string<char> convert(In&& src){ return src; }

template<typename In> requires __hidden_type_convert::FromCharView<In>
inline std::basic_string<wchar> convert(const In& raw){
    std::basic_string_view<char> src(raw);
    if(src.empty())
        return std::basic_string<wchar>();
#ifdef NWB_PLATFORM_WINDOWS
    const auto len = MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), nullptr, 0);
    assert(len != 0);
    std::basic_string<wchar> dst(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), dst.data(), len);
    return dst;
#endif
}
template<typename In> requires __hidden_type_convert::FromCharView<In>
inline std::basic_string<wchar> convert(In&& raw){
    std::basic_string_view<char> src(std::move(raw));
    if(src.empty())
        return std::basic_string<wchar>();
#ifdef NWB_PLATFORM_WINDOWS
    const auto len = MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), nullptr, 0);
    assert(len != 0);
    std::basic_string<wchar> dst(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), dst.data(), len);
    return dst;
#endif
}
template<typename In>
inline std::basic_string<wchar> convert(const In& src){ return src; }
template<typename In>
inline std::basic_string<wchar> convert(In&& src){ return src; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

