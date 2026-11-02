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

#include "platform.h"
#include "string_view.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(UNICODE) || defined(_UNICODE)
#define NWB_UNICODE
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


typedef bool b8;

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
#if defined(NWB_UNICODE)
typedef wchar tchar;
#else
typedef char tchar;
#endif

using WStringView = BasicStringView<wchar>;
using TStringView = BasicStringView<tchar>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MaxAlign = std::max_align_t;
using Nullptr = std::nullptr_t;

inline constexpr usize s_ByteTypeByteSize = 1u;
inline constexpr usize s_AlignRemainderZero = 0u;

using GeneralException = std::exception;

using RuntimeException = std::runtime_error;

using ErrorCode = std::error_code;

template<typename T>
using InitializerList = std::initializer_list<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_UNICODE)
#define NWB_DETAIL_TEXT(x) L ## x
#else
#define NWB_DETAIL_TEXT(x) x
#endif
#define NWB_TEXT(x) NWB_DETAIL_TEXT(x)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type_borrow.h"
#include "type_properties.h"
#include "compile.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

