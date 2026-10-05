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
#define GLB_UNICODE
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
#if defined(GLB_UNICODE)
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


#if defined(GLB_UNICODE)
#define GLB_DETAIL_TEXT(x) L ## x
#else
#define GLB_DETAIL_TEXT(x) x
#endif
#define GLB_TEXT(x) GLB_DETAIL_TEXT(x)

#if defined(GLB_PLATFORM_WINDOWS)
#if GLB_COMPILER_FRONTEND_MSVC || __has_declspec_attribute(dllexport)
#define GLB_DLL_EXPORT __declspec(dllexport)
#define GLB_DLL_IMPORT __declspec(dllimport)
#elif __has_attribute(dllexport)
#define GLB_DLL_EXPORT __attribute__((dllexport))
#define GLB_DLL_IMPORT __attribute__((dllimport))
#else
#define GLB_DLL_EXPORT
#define GLB_DLL_IMPORT
#endif
#elif (defined(GLB_PLATFORM_UNIX) || defined(GLB_PLATFORM_APPLE))
#define GLB_DLL_EXPORT __attribute__((visibility("default")))
#define GLB_DLL_IMPORT
#else
#define GLB_DLL_EXPORT
#define GLB_DLL_IMPORT
#endif

#if defined(GLB_EXPORT_DLL)
#define GLB_DLL_API GLB_DLL_EXPORT
#else
#define GLB_DLL_API GLB_DLL_IMPORT
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

