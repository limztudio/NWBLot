// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <exception>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <system_error>
#include <type_traits>

#include "type_borrow.h"
#include "type_properties.h"
#include "compile.h"
#include "platform.h"
#include "string_view.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(UNICODE) || defined(_UNICODE)
#define GLOBAL_UNICODE
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

typedef wchar_t wchar;
#if defined(GLOBAL_UNICODE)
typedef wchar tchar;
#else
typedef char tchar;
#endif

using WStringView = BasicStringView<wchar>;
using TStringView = BasicStringView<tchar>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MaxAlign = std::max_align_t;


using GeneralException = std::exception;

using RuntimeException = std::runtime_error;


using ErrorCode = std::error_code;


template<typename T>
using InitializerList = std::initializer_list<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(GLOBAL_UNICODE)
#define __GLOBAL_TEXT(x) L ## x
#else
#define __GLOBAL_TEXT(x) x
#endif
#define GLOBAL_TEXT(x) __GLOBAL_TEXT(x)

#if defined(GLOBAL_PLATFORM_WINDOWS)
#if GLOBAL_COMPILER_FRONTEND_MSVC || __has_declspec_attribute(dllexport)
#define GLOBAL_DLL_EXPORT __declspec(dllexport)
#define GLOBAL_DLL_IMPORT __declspec(dllimport)
#elif __has_attribute(dllexport)
#define GLOBAL_DLL_EXPORT __attribute__((dllexport))
#define GLOBAL_DLL_IMPORT __attribute__((dllimport))
#else
#define GLOBAL_DLL_EXPORT
#define GLOBAL_DLL_IMPORT
#endif
#elif (defined(GLOBAL_PLATFORM_UNIX) || defined(GLOBAL_PLATFORM_APPLE))
#define GLOBAL_DLL_EXPORT __attribute__((visibility("default")))
#define GLOBAL_DLL_IMPORT
#else
#define GLOBAL_DLL_EXPORT
#define GLOBAL_DLL_IMPORT
#endif

#if defined(GLOBAL_EXPORT_DLL)
#define GLOBAL_DLL_API GLOBAL_DLL_EXPORT
#else
#define GLOBAL_DLL_API GLOBAL_DLL_IMPORT
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

