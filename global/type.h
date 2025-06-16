// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <type_traits>

#include "type_borrow.h"
#include "type_properties.h"

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

using isize = Conditional_T<sizeof(void*) == 8, __int64, int>;
using usize = Conditional_T<sizeof(void*) == 8, unsigned __int64, unsigned int>;

typedef float f32;
typedef double f64;

//typedef char char;
typedef wchar_t wchar;
#if defined(NWB_UNICODE)
typedef wchar tchar;
#else
typedef char tchar;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_UNICODE)
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

