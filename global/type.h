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
typedef wchar_t uchar;
#ifdef NWB_UNICODE
typedef uchar tchar;
#else
typedef char tchar;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_PLATFORM_WINDOWS
#include <windows.h>
#endif
template <typename In, typename Out>
inline void convert(In src, std::basic_string<Out>& dst){ dst = src; }
template <>
inline void convert(std::basic_string_view<wchar_t> src, std::basic_string<char>& dst){
    if(src.empty()){
        dst.clear();
        return;
    }
#ifdef NWB_PLATFORM_WINDOWS
    const auto len = WideCharToMultiByte(CP_ACP, 0, src.data(), static_cast<int>(src.length()), nullptr, 0, nullptr, nullptr);
    assert(len != 0);
    dst.resize(len, 0);
    WideCharToMultiByte(CP_ACP, 0, src.data(), static_cast<int>(src.length()), dst.data(), len, nullptr, nullptr);
#endif
}
template <>
inline void convert(const wchar_t* src, std::basic_string<char>& dst){ return convert(std::basic_string_view<wchar_t>(src), dst); }
template <>
inline void convert(std::basic_string_view<char> src, std::basic_string<wchar_t>& dst){
    if (src.empty()){
        dst.clear();
        return;
    }
#ifdef NWB_PLATFORM_WINDOWS
    const auto len = MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), nullptr, 0);
    assert(len != 0);
    MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), dst.data(), len);
#endif
}
template <>
inline void convert(const char* src, std::basic_string<wchar_t>& dst){ return convert(std::basic_string_view<char>(src), dst); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

