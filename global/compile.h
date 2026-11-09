// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <cerrno>
#include <clocale>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef interface
#define interface struct
#endif

#if !defined(__clang__)
#error "A Clang-based compiler is required."
#endif

#if defined(FOUNDATION_DEBUG)
#define FOUNDATION_INLINE inline
#elif defined(FOUNDATION_OPTIMIZE) || defined(FOUNDATION_FINAL)
#define FOUNDATION_INLINE inline __attribute__((always_inline))
#endif

#define FOUNDATION_NOINLINE __attribute__((noinline))


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define FOUNDATION_DEBUGTRAP __builtin_debugtrap()

#if defined(FOUNDATION_DEBUG)
#define FOUNDATION_OCCUR_INFO true
#define FOUNDATION_OCCUR_ESSENTIAL_INFO true
#define FOUNDATION_OCCUR_ASSERT true
#define FOUNDATION_OCCUR_FATAL_ASSERT true
#define FOUNDATION_OCCUR_WARNING true
#define FOUNDATION_OCCUR_CRITICAL_WARNING true
#define FOUNDATION_OCCUR_ERROR true
#define FOUNDATION_HARDBREAK FOUNDATION_DEBUGTRAP
#define FOUNDATION_SOFTBREAK FOUNDATION_DEBUGTRAP
#elif defined(FOUNDATION_OPTIMIZE)
#define FOUNDATION_OCCUR_INFO true
#define FOUNDATION_OCCUR_ESSENTIAL_INFO true
#define FOUNDATION_OCCUR_ASSERT false
#define FOUNDATION_OCCUR_FATAL_ASSERT true
#define FOUNDATION_OCCUR_WARNING true
#define FOUNDATION_OCCUR_CRITICAL_WARNING true
#define FOUNDATION_OCCUR_ERROR true
#define FOUNDATION_HARDBREAK
#define FOUNDATION_SOFTBREAK FOUNDATION_DEBUGTRAP
#else
#define FOUNDATION_OCCUR_INFO false
#define FOUNDATION_OCCUR_ESSENTIAL_INFO true
#define FOUNDATION_OCCUR_ASSERT false
#define FOUNDATION_OCCUR_FATAL_ASSERT true
#define FOUNDATION_OCCUR_WARNING false
#define FOUNDATION_OCCUR_CRITICAL_WARNING true
#define FOUNDATION_OCCUR_ERROR true
#define FOUNDATION_HARDBREAK
#define FOUNDATION_SOFTBREAK
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define FOUNDATION_COUT ::std::cout
#define FOUNDATION_CIN ::std::cin
#define FOUNDATION_CERR ::std::cerr
#define FOUNDATION_STRLEN(src) strlen(src)
#define FOUNDATION_MEMCMP(lhs, rhs, size) memcmp(lhs, rhs, size)
#define FOUNDATION_STRCMP(lhs, rhs) strcmp(lhs, rhs)
#define FOUNDATION_MEMSET(dest, value, size) memset(dest, value, size)
#if defined(_MSC_VER)
#define FOUNDATION_STRNLEN(src, count) strnlen_s(src, count)
#define FOUNDATION_MEMCPY(dest, destSize, src, srcSize) memcpy_s(dest, destSize, src, srcSize)
#define FOUNDATION_STRCPY(dest, destSize, src) strcpy_s(dest, destSize, src)
#define FOUNDATION_STRCAT(dest, destSize, src) strcat_s(dest, destSize, src)
#define FOUNDATION_SPRINTF(format, formatSize, ...) sprintf_s(format, formatSize, __VA_ARGS__)
#define FOUNDATION_STRERROR(dest, destSize, errorNum) strerror_s(dest, destSize, errorNum)
#else
namespace CompileDetail{
template<typename DestT, typename SrcT>
inline DestT* CheckedMemcpy(DestT* dest, const std::size_t destSize, const SrcT* src, const std::size_t srcSize)noexcept{
    if(srcSize == 0u)
        return dest;
    if(srcSize > destSize)
        std::abort();
    if(dest == nullptr || src == nullptr)
        std::abort();

    return static_cast<DestT*>(std::memcpy(dest, src, srcSize));
}

inline std::size_t BoundedLength(const char* text, const std::size_t maxLength)noexcept{
    return ::strnlen(text, maxLength);
}

inline std::size_t BoundedLength(const wchar_t* text, const std::size_t maxLength)noexcept{
    return ::wcsnlen(text, maxLength);
}

template<typename CharT>
inline int BoundedCopy(CharT* dest, const std::size_t destSize, const CharT* src)noexcept{
    if(dest == nullptr || destSize == 0u)
        return -1;
    if(src == nullptr){
        dest[0] = CharT{};
        return -1;
    }

    const std::size_t srcLength = BoundedLength(src, destSize);
    const std::size_t copyCount = srcLength < (destSize - 1u) ? srcLength : (destSize - 1u);
    if(copyCount > 0u)
        std::char_traits<CharT>::copy(dest, src, copyCount);
    dest[copyCount] = CharT{};
    return copyCount == srcLength ? 0 : -1;
}

template<typename CharT>
inline int BoundedCat(CharT* dest, const std::size_t destSize, const CharT* src)noexcept{
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

inline int BoundedStrError(char* dest, const std::size_t destSize, const int errorNum)noexcept{
    return BoundedCopy(dest, destSize, std::strerror(errorNum));
}
};

#define FOUNDATION_STRNLEN(src, count) strnlen(src, count)
#define FOUNDATION_MEMCPY(dest, destSize, src, srcSize) ::CompileDetail::CheckedMemcpy(dest, destSize, src, srcSize)
#define FOUNDATION_STRCPY(dest, destSize, src) ::CompileDetail::BoundedCopy(dest, destSize, src)
#define FOUNDATION_STRCAT(dest, destSize, src) ::CompileDetail::BoundedCat(dest, destSize, src)
#define FOUNDATION_SPRINTF(format, formatSize, ...) snprintf(format, formatSize, __VA_ARGS__)
#define FOUNDATION_STRERROR(dest, destSize, errorNum) ::CompileDetail::BoundedStrError(dest, destSize, errorNum)
#endif

#if defined(UNICODE) || defined(_UNICODE)
#define FOUNDATION_TCOUT ::std::wcout
#define FOUNDATION_TCERR ::std::wcerr
#if defined(_MSC_VER)
#define FOUNDATION_TSPRINTF(format, formatSize, ...) swprintf_s(format, formatSize, __VA_ARGS__)
#else
#define FOUNDATION_TSPRINTF(format, formatSize, ...) swprintf(format, formatSize, __VA_ARGS__)
#endif
#else
#define FOUNDATION_TCOUT FOUNDATION_COUT
#define FOUNDATION_TCERR FOUNDATION_CERR
#define FOUNDATION_TSPRINTF(format, formatSize, ...) FOUNDATION_SPRINTF(format, formatSize, __VA_ARGS__)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline char* SetLocale(const int category, const char* locale)noexcept{
    return std::setlocale(category, locale);
}

[[nodiscard]] constexpr b8 CanEnableDebugRuntime()noexcept{
#if !defined(FOUNDATION_FINAL)
    return true;
#else
    return false;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

