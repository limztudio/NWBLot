// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#ifndef __has_declspec_attribute
#define __has_declspec_attribute(x) 0
#endif

#if defined(__clang__)
#define NWB_COMPILER_CLANG 1
#else
#define NWB_COMPILER_CLANG 0
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define NWB_COMPILER_MSVC 1
#else
#define NWB_COMPILER_MSVC 0
#endif

#if defined(__clang_cl__) || (defined(_MSC_VER) && !defined(__clang__))
#define NWB_COMPILER_FRONTEND_MSVC 1
#else
#define NWB_COMPILER_FRONTEND_MSVC 0
#endif

#if !NWB_COMPILER_FRONTEND_MSVC
#define NWB_COMPILER_FRONTEND_GNU 1
#else
#define NWB_COMPILER_FRONTEND_GNU 0
#endif

#if defined(PROP_DBG)
#define NWB_DEBUG
#elif defined(PROP_OPT)
#define NWB_OPTIMIZE
#elif defined(PROP_FIN)
#define NWB_FINAL
#endif

#if !defined(NWB_DEBUG) && (defined(DEBUG) || defined(_DEBUG))
#define NWB_DEBUG
#endif

#if !defined(NWB_OPTIMIZE) && !defined(NWB_FINAL) && (defined(NDEBUG) || defined(_NDEBUG))
#define NWB_OPTIMIZE
#endif

#if defined(NWB_DEBUG)
#define NWB_INLINE inline
#elif defined(NWB_OPTIMIZE) || defined(NWB_FINAL)
#if NWB_COMPILER_FRONTEND_MSVC
#define NWB_INLINE __forceinline
#elif __has_attribute(always_inline) || defined(__GNUC__)
#define NWB_INLINE inline __attribute__((always_inline))
#else
#define NWB_INLINE inline
#endif
#endif

#if __has_attribute(vectorcall)
#define NWB_VECTORCALL __attribute__((vectorcall))
#elif NWB_COMPILER_FRONTEND_MSVC
#define NWB_VECTORCALL __vectorcall
#else
#define NWB_VECTORCALL
#endif

#if NWB_COMPILER_FRONTEND_MSVC
#define NWB_ALLOCATOR_PREFIX __declspec(allocator)
#define NWB_ALLOCATOR_SUFFIX
#elif __has_attribute(malloc) || defined(__GNUC__)
#define NWB_ALLOCATOR_PREFIX
#define NWB_ALLOCATOR_SUFFIX __attribute__((malloc))
#else
#define NWB_ALLOCATOR_PREFIX
#define NWB_ALLOCATOR_SUFFIX
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if NWB_COMPILER_FRONTEND_MSVC
#define NWB_DEBUGTRAP __debugbreak()
#elif __has_builtin(__builtin_debugtrap)
#define NWB_DEBUGTRAP __builtin_debugtrap()
#elif __has_builtin(__builtin_trap)
#define NWB_DEBUGTRAP __builtin_trap()
#else
#define NWB_DEBUGTRAP
#endif

#if defined(NWB_DEBUG)
#define NWB_OCCUR_INFO true
#define NWB_OCCUR_ESSENTIAL_INFO true
#define NWB_OCCUR_ASSERT true
#define NWB_OCCUR_FATAL_ASSERT true
#define NWB_OCCUR_WARNING true
#define NWB_OCCUR_CRITICAL_WARNING true
#define NWB_OCCUR_ERROR true
#define NWB_HARDBREAK NWB_DEBUGTRAP
#define NWB_SOFTBREAK NWB_DEBUGTRAP
#elif defined(NWB_OPTIMIZE)
#define NWB_OCCUR_INFO true
#define NWB_OCCUR_ESSENTIAL_INFO true
#define NWB_OCCUR_ASSERT false
#define NWB_OCCUR_FATAL_ASSERT true
#define NWB_OCCUR_WARNING true
#define NWB_OCCUR_CRITICAL_WARNING true
#define NWB_OCCUR_ERROR true
#define NWB_HARDBREAK
#define NWB_SOFTBREAK NWB_DEBUGTRAP
#else
#define NWB_OCCUR_INFO false
#define NWB_OCCUR_ESSENTIAL_INFO true
#define NWB_OCCUR_ASSERT false
#define NWB_OCCUR_FATAL_ASSERT true
#define NWB_OCCUR_WARNING false
#define NWB_OCCUR_CRITICAL_WARNING true
#define NWB_OCCUR_ERROR true
#define NWB_HARDBREAK
#define NWB_SOFTBREAK
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_COUT ::std::cout
#define NWB_CIN ::std::cin
#define NWB_WCOUT ::std::wcout
#define NWB_WCIN ::std::wcin
#define NWB_CERR ::std::cerr
#define NWB_WCERR ::std::wcerr
#define NWB_GETENV(name) ::std::getenv(name)
#define NWB_STRLEN(src) strlen(src)
#define NWB_WSTRLEN(src) wcslen(src)
#define NWB_MEMCMP(lhs, rhs, size) memcmp(lhs, rhs, size)
#define NWB_STRCMP(lhs, rhs) strcmp(lhs, rhs)
#define NWB_WSTRCMP(lhs, rhs) wcscmp(lhs, rhs)
#define NWB_MEMSET(dest, value, size) memset(dest, value, size)
#if defined(_MSC_VER)
#define NWB_STRNLEN(src, count) strnlen_s(src, count)
#define NWB_WSTRNLEN(src, count) wcsnlen_s(src, count)
#define NWB_MEMCPY(dest, destSize, src, srcSize) memcpy_s(dest, destSize, src, srcSize)
#define NWB_WMEMCPY(dest, destSize, src, srcSize) wmemcpy_s(dest, destSize, src, srcSize)
#define NWB_STRCPY(dest, destSize, src) strcpy_s(dest, destSize, src)
#define NWB_WSTRCPY(dest, destSize, src) wcscpy_s(dest, destSize, src)
#define NWB_STRNCPY(dest, destSize, src, count) strncpy_s(dest, destSize, src, count)
#define NWB_WSTRNCPY(dest, destSize, src, count) wcsncpy_s(dest, destSize, src, count)
#define NWB_STRCAT(dest, destSize, src) strcat_s(dest, destSize, src)
#define NWB_WSTRCAT(dest, destSize, src) wcscat_s(dest, destSize, src)
#define NWB_SPRINTF(format, formatSize, ...) sprintf_s(format, formatSize, __VA_ARGS__)
#define NWB_WSPRINTF(format, formatSize, ...) swprintf_s(format, formatSize, __VA_ARGS__)
#define NWB_VSNPRINTF(dest, destSize, format, args) vsnprintf_s(dest, destSize, _TRUNCATE, format, args)
#define NWB_VWSNPRINTF(dest, destSize, format, args) vswprintf_s(dest, destSize, format, args)
#define NWB_STRERROR(dest, destSize, errorNum) strerror_s(dest, destSize, errorNum)
#else
namespace CompileDetail{
template<typename DestT, typename SrcT>
inline DestT* CheckedMemcpy(DestT* dest, const std::size_t destSize, const SrcT* src, const std::size_t srcSize){
    if(srcSize == 0u)
        return dest;
    if(srcSize > destSize)
        std::abort();
    if(dest == nullptr || src == nullptr)
        std::abort();

    return static_cast<DestT*>(std::memcpy(dest, src, srcSize));
}

template<typename DestT, typename SrcT>
inline DestT* CheckedWmemcpy(DestT* dest, const std::size_t destSize, const SrcT* src, const std::size_t srcSize){
    if(srcSize == 0u)
        return dest;
    if(srcSize > destSize)
        std::abort();
    if(dest == nullptr || src == nullptr)
        std::abort();

    return std::wmemcpy(dest, src, srcSize);
}

inline std::size_t BoundedLength(const char* text, const std::size_t maxLength){
    return ::strnlen(text, maxLength);
}

inline std::size_t BoundedLength(const wchar_t* text, const std::size_t maxLength){
    return ::wcsnlen(text, maxLength);
}

template<typename CharT>
inline int BoundedCopy(CharT* dest, const std::size_t destSize, const CharT* src){
    if(dest == nullptr || destSize == 0u)
        return -1;
    if(src == nullptr){
        dest[0] = CharT{};
        return -1;
    }

    const std::size_t srcLength = BoundedLength(src, destSize);
    const std::size_t copyCount = srcLength < (destSize - 1u)
        ? srcLength
        : (destSize - 1u)
    ;
    if(copyCount > 0u)
        std::char_traits<CharT>::copy(dest, src, copyCount);
    dest[copyCount] = CharT{};
    return copyCount == srcLength ? 0 : -1;
}

template<typename CharT>
inline int BoundedNCopy(CharT* dest, const std::size_t destSize, const CharT* src, const std::size_t count){
    if(dest == nullptr || destSize == 0u)
        return -1;
    if(src == nullptr){
        dest[0] = CharT{};
        return -1;
    }

    const std::size_t requestedCount = BoundedLength(src, count);
    const std::size_t copyCount = requestedCount < (destSize - 1u)
        ? requestedCount
        : (destSize - 1u)
    ;
    if(copyCount > 0u)
        std::char_traits<CharT>::copy(dest, src, copyCount);
    dest[copyCount] = CharT{};
    return copyCount == requestedCount ? 0 : -1;
}

template<typename CharT>
inline int BoundedCat(CharT* dest, const std::size_t destSize, const CharT* src){
    if(dest == nullptr || destSize == 0u)
        return -1;
    if(src == nullptr)
        return -1;

    const std::size_t destLength = BoundedLength(dest, destSize);
    if(destLength >= destSize){
        dest[destSize - 1u] = CharT{};
        return -1;
    }

    const int result = BoundedCopy(dest + destLength, destSize - destLength, src);
    return result == 0 ? 0 : -1;
}

inline int BoundedStrError(char* dest, const std::size_t destSize, const int errorNum){
    return BoundedCopy(dest, destSize, std::strerror(errorNum));
}
};

#define NWB_STRNLEN(src, count) strnlen(src, count)
#define NWB_WSTRNLEN(src, count) wcsnlen(src, count)
#define NWB_MEMCPY(dest, destSize, src, srcSize) ::CompileDetail::CheckedMemcpy(dest, destSize, src, srcSize)
#define NWB_WMEMCPY(dest, destSize, src, srcSize) ::CompileDetail::CheckedWmemcpy(dest, destSize, src, srcSize)
#define NWB_STRCPY(dest, destSize, src) ::CompileDetail::BoundedCopy(dest, destSize, src)
#define NWB_WSTRCPY(dest, destSize, src) ::CompileDetail::BoundedCopy(dest, destSize, src)
#define NWB_STRNCPY(dest, destSize, src, count) ::CompileDetail::BoundedNCopy(dest, destSize, src, count)
#define NWB_WSTRNCPY(dest, destSize, src, count) ::CompileDetail::BoundedNCopy(dest, destSize, src, count)
#define NWB_STRCAT(dest, destSize, src) ::CompileDetail::BoundedCat(dest, destSize, src)
#define NWB_WSTRCAT(dest, destSize, src) ::CompileDetail::BoundedCat(dest, destSize, src)
#define NWB_SPRINTF(format, formatSize, ...) snprintf(format, formatSize, __VA_ARGS__)
#define NWB_WSPRINTF(format, formatSize, ...) swprintf(format, formatSize, __VA_ARGS__)
#define NWB_VSNPRINTF(dest, destSize, format, args) vsnprintf(dest, destSize, format, args)
#define NWB_VWSNPRINTF(dest, destSize, format, args) vswprintf(dest, destSize, format, args)
#define NWB_STRERROR(dest, destSize, errorNum) ::CompileDetail::BoundedStrError(dest, destSize, errorNum)
#endif

#if defined(UNICODE) || defined(_UNICODE)
#define NWB_TCOUT NWB_WCOUT
#define NWB_TCIN NWB_WCIN
#define NWB_TCERR NWB_WCERR
#define NWB_TSTRLEN(src) NWB_WSTRLEN(src)
#define NWB_TSTRNLEN(src, count) NWB_WSTRNLEN(src, count)
#define NWB_TSTRCMP(lhs, rhs) NWB_WSTRCMP(lhs, rhs)
#define NWB_TMEMCPY(dest, destSize, src, srcSize) NWB_WMEMCPY(dest, destSize, src, srcSize)
#define NWB_TSTRCPY(dest, destSize, src) NWB_WSTRCPY(dest, destSize, src)
#define NWB_TSTRNCPY(dest, destSize, src, count) NWB_WSTRNCPY(dest, destSize, src, count)
#define NWB_TSTRCAT(dest, destSize, src) NWB_WSTRCAT(dest, destSize, src)
#define NWB_TSPRINTF(format, formatSize, ...) NWB_WSPRINTF(format, formatSize, __VA_ARGS__)
#define NWB_TVSNPRINTF(dest, destSize, format, args) NWB_VWSNPRINTF(dest, destSize, format, args)
#else
#define NWB_TCOUT NWB_COUT
#define NWB_TCIN NWB_CIN
#define NWB_TCERR NWB_CERR
#define NWB_TSTRLEN(src) NWB_STRLEN(src)
#define NWB_TSTRNLEN(src, count) NWB_STRNLEN(src, count)
#define NWB_TSTRCMP(lhs, rhs) NWB_STRCMP(lhs, rhs)
#define NWB_TMEMCPY(dest, destSize, src, srcSize) NWB_MEMCPY(dest, destSize, src, srcSize)
#define NWB_TSTRCPY(dest, destSize, src) NWB_STRCPY(dest, destSize, src)
#define NWB_TSTRNCPY(dest, destSize, src, count) NWB_STRNCPY(dest, destSize, src, count)
#define NWB_TSTRCAT(dest, destSize, src) NWB_STRCAT(dest, destSize, src)
#define NWB_TSPRINTF(format, formatSize, ...) NWB_SPRINTF(format, formatSize, __VA_ARGS__)
#define NWB_TVSNPRINTF(dest, destSize, format, args) NWB_VSNPRINTF(dest, destSize, format, args)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr bool CanEnableDebugRuntime(){
#if defined(NWB_DEBUG)
    return true;
#else
    return false;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

