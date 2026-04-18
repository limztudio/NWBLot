// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <exception>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <system_error>
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

typedef int i32;
typedef unsigned int u32;

using i64 = std::int64_t;
using u64 = std::uint64_t;

using isize = std::intptr_t;
using usize = std::uintptr_t;

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


using MaxAlign = std::max_align_t;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using GeneralException = std::exception;

using RuntimeException = std::runtime_error;


using ErrorCode = std::error_code;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_UNICODE)
#define __NWB_TEXT(x) L ## x
#else
#define __NWB_TEXT(x) x
#endif
#define NWB_TEXT(x) __NWB_TEXT(x)

#if defined(NWB_PLATFORM_WINDOWS)
#if NWB_COMPILER_FRONTEND_MSVC || __has_declspec_attribute(dllexport)
#define NWB_DLL_EXPORT __declspec(dllexport)
#define NWB_DLL_IMPORT __declspec(dllimport)
#elif __has_attribute(dllexport)
#define NWB_DLL_EXPORT __attribute__((dllexport))
#define NWB_DLL_IMPORT __attribute__((dllimport))
#else
#define NWB_DLL_EXPORT
#define NWB_DLL_IMPORT
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

