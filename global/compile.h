// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef interface
#define interface struct
#endif

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
#define GLOBAL_COMPILER_CLANG 1
#else
#define GLOBAL_COMPILER_CLANG 0
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define GLOBAL_COMPILER_MSVC 1
#else
#define GLOBAL_COMPILER_MSVC 0
#endif

#if defined(__clang_cl__) || (defined(_MSC_VER) && !defined(__clang__))
#define GLOBAL_COMPILER_FRONTEND_MSVC 1
#else
#define GLOBAL_COMPILER_FRONTEND_MSVC 0
#endif

#if !GLOBAL_COMPILER_FRONTEND_MSVC
#define GLOBAL_COMPILER_FRONTEND_GNU 1
#else
#define GLOBAL_COMPILER_FRONTEND_GNU 0
#endif

#if defined(PROP_DBG)
#define GLOBAL_DEBUG
#elif defined(PROP_OPT)
#define GLOBAL_OPTIMIZE
#elif defined(PROP_FIN)
#define GLOBAL_FINAL
#endif

#if !defined(GLOBAL_DEBUG) && (defined(DEBUG) || defined(_DEBUG))
#define GLOBAL_DEBUG
#endif

#if !defined(GLOBAL_OPTIMIZE) && !defined(GLOBAL_FINAL) && (defined(NDEBUG) || defined(_NDEBUG))
#define GLOBAL_OPTIMIZE
#endif

#if defined(GLOBAL_DEBUG)
#define GLOBAL_INLINE inline
#elif defined(GLOBAL_OPTIMIZE) || defined(GLOBAL_FINAL)
#if GLOBAL_COMPILER_FRONTEND_MSVC
#define GLOBAL_INLINE __forceinline
#elif __has_attribute(always_inline) || defined(__GNUC__)
#define GLOBAL_INLINE inline __attribute__((always_inline))
#else
#define GLOBAL_INLINE inline
#endif
#endif

#if GLOBAL_COMPILER_FRONTEND_MSVC
#define GLOBAL_NOINLINE __declspec(noinline)
#elif __has_attribute(noinline) || defined(__GNUC__)
#define GLOBAL_NOINLINE __attribute__((noinline))
#else
#define GLOBAL_NOINLINE
#endif

#if __has_attribute(vectorcall)
#define GLOBAL_VECTORCALL __attribute__((vectorcall))
#elif GLOBAL_COMPILER_FRONTEND_MSVC
#define GLOBAL_VECTORCALL __vectorcall
#else
#define GLOBAL_VECTORCALL
#endif

#if GLOBAL_COMPILER_FRONTEND_MSVC
#define GLOBAL_ALLOCATOR_PREFIX __declspec(allocator)
#define GLOBAL_ALLOCATOR_SUFFIX
#elif __has_attribute(malloc) || defined(__GNUC__)
#define GLOBAL_ALLOCATOR_PREFIX
#define GLOBAL_ALLOCATOR_SUFFIX __attribute__((malloc))
#else
#define GLOBAL_ALLOCATOR_PREFIX
#define GLOBAL_ALLOCATOR_SUFFIX
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if GLOBAL_COMPILER_FRONTEND_MSVC
#define GLOBAL_DEBUGTRAP __debugbreak()
#elif __has_builtin(__builtin_debugtrap)
#define GLOBAL_DEBUGTRAP __builtin_debugtrap()
#elif __has_builtin(__builtin_trap)
#define GLOBAL_DEBUGTRAP __builtin_trap()
#else
#define GLOBAL_DEBUGTRAP
#endif

#if defined(GLOBAL_DEBUG)
#define GLOBAL_OCCUR_INFO true
#define GLOBAL_OCCUR_ESSENTIAL_INFO true
#define GLOBAL_OCCUR_ASSERT true
#define GLOBAL_OCCUR_FATAL_ASSERT true
#define GLOBAL_OCCUR_WARNING true
#define GLOBAL_OCCUR_CRITICAL_WARNING true
#define GLOBAL_OCCUR_ERROR true
#define GLOBAL_HARDBREAK GLOBAL_DEBUGTRAP
#define GLOBAL_SOFTBREAK GLOBAL_DEBUGTRAP
#elif defined(GLOBAL_OPTIMIZE)
#define GLOBAL_OCCUR_INFO true
#define GLOBAL_OCCUR_ESSENTIAL_INFO true
#define GLOBAL_OCCUR_ASSERT false
#define GLOBAL_OCCUR_FATAL_ASSERT true
#define GLOBAL_OCCUR_WARNING true
#define GLOBAL_OCCUR_CRITICAL_WARNING true
#define GLOBAL_OCCUR_ERROR true
#define GLOBAL_HARDBREAK
#define GLOBAL_SOFTBREAK GLOBAL_DEBUGTRAP
#else
#define GLOBAL_OCCUR_INFO false
#define GLOBAL_OCCUR_ESSENTIAL_INFO true
#define GLOBAL_OCCUR_ASSERT false
#define GLOBAL_OCCUR_FATAL_ASSERT true
#define GLOBAL_OCCUR_WARNING false
#define GLOBAL_OCCUR_CRITICAL_WARNING true
#define GLOBAL_OCCUR_ERROR true
#define GLOBAL_HARDBREAK
#define GLOBAL_SOFTBREAK
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define GLOBAL_COUT ::std::cout
#define GLOBAL_CIN ::std::cin
#define GLOBAL_WCOUT ::std::wcout
#define GLOBAL_WCIN ::std::wcin
#define GLOBAL_CERR ::std::cerr
#define GLOBAL_WCERR ::std::wcerr
#define GLOBAL_STRLEN(src) strlen(src)
#define GLOBAL_WSTRLEN(src) wcslen(src)
#define GLOBAL_MEMCMP(lhs, rhs, size) memcmp(lhs, rhs, size)
#define GLOBAL_STRCMP(lhs, rhs) strcmp(lhs, rhs)
#define GLOBAL_WSTRCMP(lhs, rhs) wcscmp(lhs, rhs)
#define GLOBAL_MEMSET(dest, value, size) memset(dest, value, size)
#if defined(_MSC_VER)
#define GLOBAL_STRNLEN(src, count) strnlen_s(src, count)
#define GLOBAL_WSTRNLEN(src, count) wcsnlen_s(src, count)
#define GLOBAL_MEMCPY(dest, destSize, src, srcSize) memcpy_s(dest, destSize, src, srcSize)
#define GLOBAL_WMEMCPY(dest, destSize, src, srcSize) wmemcpy_s(dest, destSize, src, srcSize)
#define GLOBAL_STRCPY(dest, destSize, src) strcpy_s(dest, destSize, src)
#define GLOBAL_WSTRCPY(dest, destSize, src) wcscpy_s(dest, destSize, src)
#define GLOBAL_STRNCPY(dest, destSize, src, count) strncpy_s(dest, destSize, src, count)
#define GLOBAL_WSTRNCPY(dest, destSize, src, count) wcsncpy_s(dest, destSize, src, count)
#define GLOBAL_STRCAT(dest, destSize, src) strcat_s(dest, destSize, src)
#define GLOBAL_WSTRCAT(dest, destSize, src) wcscat_s(dest, destSize, src)
#define GLOBAL_SPRINTF(format, formatSize, ...) sprintf_s(format, formatSize, __VA_ARGS__)
#define GLOBAL_WSPRINTF(format, formatSize, ...) swprintf_s(format, formatSize, __VA_ARGS__)
#define GLOBAL_VSNPRINTF(dest, destSize, format, args) vsnprintf_s(dest, destSize, _TRUNCATE, format, args)
#define GLOBAL_VWSNPRINTF(dest, destSize, format, args) vswprintf_s(dest, destSize, format, args)
#define GLOBAL_STRERROR(dest, destSize, errorNum) strerror_s(dest, destSize, errorNum)
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

#define GLOBAL_STRNLEN(src, count) strnlen(src, count)
#define GLOBAL_WSTRNLEN(src, count) wcsnlen(src, count)
#define GLOBAL_MEMCPY(dest, destSize, src, srcSize) ::CompileDetail::CheckedMemcpy(dest, destSize, src, srcSize)
#define GLOBAL_WMEMCPY(dest, destSize, src, srcSize) ::CompileDetail::CheckedWmemcpy(dest, destSize, src, srcSize)
#define GLOBAL_STRCPY(dest, destSize, src) ::CompileDetail::BoundedCopy(dest, destSize, src)
#define GLOBAL_WSTRCPY(dest, destSize, src) ::CompileDetail::BoundedCopy(dest, destSize, src)
#define GLOBAL_STRNCPY(dest, destSize, src, count) ::CompileDetail::BoundedNCopy(dest, destSize, src, count)
#define GLOBAL_WSTRNCPY(dest, destSize, src, count) ::CompileDetail::BoundedNCopy(dest, destSize, src, count)
#define GLOBAL_STRCAT(dest, destSize, src) ::CompileDetail::BoundedCat(dest, destSize, src)
#define GLOBAL_WSTRCAT(dest, destSize, src) ::CompileDetail::BoundedCat(dest, destSize, src)
#define GLOBAL_SPRINTF(format, formatSize, ...) snprintf(format, formatSize, __VA_ARGS__)
#define GLOBAL_WSPRINTF(format, formatSize, ...) swprintf(format, formatSize, __VA_ARGS__)
#define GLOBAL_VSNPRINTF(dest, destSize, format, args) vsnprintf(dest, destSize, format, args)
#define GLOBAL_VWSNPRINTF(dest, destSize, format, args) vswprintf(dest, destSize, format, args)
#define GLOBAL_STRERROR(dest, destSize, errorNum) ::CompileDetail::BoundedStrError(dest, destSize, errorNum)
#endif

#if defined(UNICODE) || defined(_UNICODE)
#define GLOBAL_TCOUT GLOBAL_WCOUT
#define GLOBAL_TCIN GLOBAL_WCIN
#define GLOBAL_TCERR GLOBAL_WCERR
#define GLOBAL_TSTRLEN(src) GLOBAL_WSTRLEN(src)
#define GLOBAL_TSTRNLEN(src, count) GLOBAL_WSTRNLEN(src, count)
#define GLOBAL_TSTRCMP(lhs, rhs) GLOBAL_WSTRCMP(lhs, rhs)
#define GLOBAL_TMEMCPY(dest, destSize, src, srcSize) GLOBAL_WMEMCPY(dest, destSize, src, srcSize)
#define GLOBAL_TSTRCPY(dest, destSize, src) GLOBAL_WSTRCPY(dest, destSize, src)
#define GLOBAL_TSTRNCPY(dest, destSize, src, count) GLOBAL_WSTRNCPY(dest, destSize, src, count)
#define GLOBAL_TSTRCAT(dest, destSize, src) GLOBAL_WSTRCAT(dest, destSize, src)
#define GLOBAL_TSPRINTF(format, formatSize, ...) GLOBAL_WSPRINTF(format, formatSize, __VA_ARGS__)
#define GLOBAL_TVSNPRINTF(dest, destSize, format, args) GLOBAL_VWSNPRINTF(dest, destSize, format, args)
#else
#define GLOBAL_TCOUT GLOBAL_COUT
#define GLOBAL_TCIN GLOBAL_CIN
#define GLOBAL_TCERR GLOBAL_CERR
#define GLOBAL_TSTRLEN(src) GLOBAL_STRLEN(src)
#define GLOBAL_TSTRNLEN(src, count) GLOBAL_STRNLEN(src, count)
#define GLOBAL_TSTRCMP(lhs, rhs) GLOBAL_STRCMP(lhs, rhs)
#define GLOBAL_TMEMCPY(dest, destSize, src, srcSize) GLOBAL_MEMCPY(dest, destSize, src, srcSize)
#define GLOBAL_TSTRCPY(dest, destSize, src) GLOBAL_STRCPY(dest, destSize, src)
#define GLOBAL_TSTRNCPY(dest, destSize, src, count) GLOBAL_STRNCPY(dest, destSize, src, count)
#define GLOBAL_TSTRCAT(dest, destSize, src) GLOBAL_STRCAT(dest, destSize, src)
#define GLOBAL_TSPRINTF(format, formatSize, ...) GLOBAL_SPRINTF(format, formatSize, __VA_ARGS__)
#define GLOBAL_TVSNPRINTF(dest, destSize, format, args) GLOBAL_VSNPRINTF(dest, destSize, format, args)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr bool CanEnableDebugRuntime(){
#if !defined(GLOBAL_FINAL)
    return true;
#else
    return false;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

