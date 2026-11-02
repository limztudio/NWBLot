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
#error "NWB requires a Clang-based compiler."
#endif

#if defined(NWB_DEBUG)
#define NWB_INLINE inline
#elif defined(NWB_OPTIMIZE) || defined(NWB_FINAL)
#define NWB_INLINE inline __attribute__((always_inline))
#endif

#define NWB_NOINLINE __attribute__((noinline))


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_DEBUGTRAP __builtin_debugtrap()

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
#define NWB_CERR ::std::cerr
#define NWB_STRLEN(src) strlen(src)
#define NWB_MEMCMP(lhs, rhs, size) memcmp(lhs, rhs, size)
#define NWB_STRCMP(lhs, rhs) strcmp(lhs, rhs)
#define NWB_MEMSET(dest, value, size) memset(dest, value, size)
#if defined(_MSC_VER)
#define NWB_STRNLEN(src, count) strnlen_s(src, count)
#define NWB_MEMCPY(dest, destSize, src, srcSize) memcpy_s(dest, destSize, src, srcSize)
#define NWB_STRCPY(dest, destSize, src) strcpy_s(dest, destSize, src)
#define NWB_STRCAT(dest, destSize, src) strcat_s(dest, destSize, src)
#define NWB_SPRINTF(format, formatSize, ...) sprintf_s(format, formatSize, __VA_ARGS__)
#define NWB_STRERROR(dest, destSize, errorNum) strerror_s(dest, destSize, errorNum)
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

#define NWB_STRNLEN(src, count) strnlen(src, count)
#define NWB_MEMCPY(dest, destSize, src, srcSize) ::CompileDetail::CheckedMemcpy(dest, destSize, src, srcSize)
#define NWB_STRCPY(dest, destSize, src) ::CompileDetail::BoundedCopy(dest, destSize, src)
#define NWB_STRCAT(dest, destSize, src) ::CompileDetail::BoundedCat(dest, destSize, src)
#define NWB_SPRINTF(format, formatSize, ...) snprintf(format, formatSize, __VA_ARGS__)
#define NWB_STRERROR(dest, destSize, errorNum) ::CompileDetail::BoundedStrError(dest, destSize, errorNum)
#endif

#if defined(UNICODE) || defined(_UNICODE)
#define NWB_TCOUT ::std::wcout
#define NWB_TCERR ::std::wcerr
#if defined(_MSC_VER)
#define NWB_TSPRINTF(format, formatSize, ...) swprintf_s(format, formatSize, __VA_ARGS__)
#else
#define NWB_TSPRINTF(format, formatSize, ...) swprintf(format, formatSize, __VA_ARGS__)
#endif
#else
#define NWB_TCOUT NWB_COUT
#define NWB_TCERR NWB_CERR
#define NWB_TSPRINTF(format, formatSize, ...) NWB_SPRINTF(format, formatSize, __VA_ARGS__)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline char* SetLocale(const int category, const char* locale)noexcept{
    return std::setlocale(category, locale);
}

[[nodiscard]] constexpr b8 CanEnableDebugRuntime()noexcept{
#if !defined(NWB_FINAL)
    return true;
#else
    return false;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

