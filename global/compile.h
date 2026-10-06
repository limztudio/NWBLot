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
#define GLB_COMPILER_CLANG 1
#else
#define GLB_COMPILER_CLANG 0
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define GLB_COMPILER_MSVC 1
#else
#define GLB_COMPILER_MSVC 0
#endif

#if defined(__clang_cl__) || (defined(_MSC_VER) && !defined(__clang__))
#define GLB_COMPILER_FRONTEND_MSVC 1
#else
#define GLB_COMPILER_FRONTEND_MSVC 0
#endif

#if !GLB_COMPILER_FRONTEND_MSVC
#define GLB_COMPILER_FRONTEND_GNU 1
#else
#define GLB_COMPILER_FRONTEND_GNU 0
#endif

#if defined(PROP_DBG)
#define GLB_DEBUG
#elif defined(PROP_OPT)
#define GLB_OPTIMIZE
#elif defined(PROP_FIN)
#define GLB_FINAL
#endif

#if !defined(GLB_DEBUG) && (defined(DEBUG) || defined(_DEBUG))
#define GLB_DEBUG
#endif

#if !defined(GLB_OPTIMIZE) && !defined(GLB_FINAL) && (defined(NDEBUG) || defined(_NDEBUG))
#define GLB_OPTIMIZE
#endif

#if defined(GLB_DEBUG)
#define GLB_INLINE inline
#elif defined(GLB_OPTIMIZE) || defined(GLB_FINAL)
#if GLB_COMPILER_FRONTEND_MSVC
#define GLB_INLINE __forceinline
#elif __has_attribute(always_inline) || defined(__GNUC__)
#define GLB_INLINE inline __attribute__((always_inline))
#else
#define GLB_INLINE inline
#endif
#endif

#if GLB_COMPILER_FRONTEND_MSVC
#define GLB_NOINLINE __declspec(noinline)
#elif __has_attribute(noinline) || defined(__GNUC__)
#define GLB_NOINLINE __attribute__((noinline))
#else
#define GLB_NOINLINE
#endif

#if __has_attribute(vectorcall)
#define GLB_VECTORCALL __attribute__((vectorcall))
#elif GLB_COMPILER_FRONTEND_MSVC
#define GLB_VECTORCALL __vectorcall
#else
#define GLB_VECTORCALL
#endif

#if GLB_COMPILER_FRONTEND_MSVC
#define GLB_ALLOCATOR_PREFIX __declspec(allocator)
#define GLB_ALLOCATOR_SUFFIX
#elif __has_attribute(malloc) || defined(__GNUC__)
#define GLB_ALLOCATOR_PREFIX
#define GLB_ALLOCATOR_SUFFIX __attribute__((malloc))
#else
#define GLB_ALLOCATOR_PREFIX
#define GLB_ALLOCATOR_SUFFIX
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if GLB_COMPILER_FRONTEND_MSVC
#define GLB_DEBUGTRAP __debugbreak()
#elif __has_builtin(__builtin_debugtrap)
#define GLB_DEBUGTRAP __builtin_debugtrap()
#elif __has_builtin(__builtin_trap)
#define GLB_DEBUGTRAP __builtin_trap()
#else
#define GLB_DEBUGTRAP
#endif

#if defined(GLB_DEBUG)
#define GLB_OCCUR_INFO true
#define GLB_OCCUR_ESSENTIAL_INFO true
#define GLB_OCCUR_ASSERT true
#define GLB_OCCUR_FATAL_ASSERT true
#define GLB_OCCUR_WARNING true
#define GLB_OCCUR_CRITICAL_WARNING true
#define GLB_OCCUR_ERROR true
#define GLB_HARDBREAK GLB_DEBUGTRAP
#define GLB_SOFTBREAK GLB_DEBUGTRAP
#elif defined(GLB_OPTIMIZE)
#define GLB_OCCUR_INFO true
#define GLB_OCCUR_ESSENTIAL_INFO true
#define GLB_OCCUR_ASSERT false
#define GLB_OCCUR_FATAL_ASSERT true
#define GLB_OCCUR_WARNING true
#define GLB_OCCUR_CRITICAL_WARNING true
#define GLB_OCCUR_ERROR true
#define GLB_HARDBREAK
#define GLB_SOFTBREAK GLB_DEBUGTRAP
#else
#define GLB_OCCUR_INFO false
#define GLB_OCCUR_ESSENTIAL_INFO true
#define GLB_OCCUR_ASSERT false
#define GLB_OCCUR_FATAL_ASSERT true
#define GLB_OCCUR_WARNING false
#define GLB_OCCUR_CRITICAL_WARNING true
#define GLB_OCCUR_ERROR true
#define GLB_HARDBREAK
#define GLB_SOFTBREAK
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define GLB_COUT ::std::cout
#define GLB_CIN ::std::cin
#define GLB_WCOUT ::std::wcout
#define GLB_WCIN ::std::wcin
#define GLB_CERR ::std::cerr
#define GLB_WCERR ::std::wcerr
#define GLB_STRLEN(src) strlen(src)
#define GLB_WSTRLEN(src) wcslen(src)
#define GLB_MEMCMP(lhs, rhs, size) memcmp(lhs, rhs, size)
#define GLB_STRCMP(lhs, rhs) strcmp(lhs, rhs)
#define GLB_WSTRCMP(lhs, rhs) wcscmp(lhs, rhs)
#define GLB_MEMSET(dest, value, size) memset(dest, value, size)
#if defined(_MSC_VER)
#define GLB_STRNLEN(src, count) strnlen_s(src, count)
#define GLB_WSTRNLEN(src, count) wcsnlen_s(src, count)
#define GLB_MEMCPY(dest, destSize, src, srcSize) memcpy_s(dest, destSize, src, srcSize)
#define GLB_WMEMCPY(dest, destSize, src, srcSize) wmemcpy_s(dest, destSize, src, srcSize)
#define GLB_STRCPY(dest, destSize, src) strcpy_s(dest, destSize, src)
#define GLB_WSTRCPY(dest, destSize, src) wcscpy_s(dest, destSize, src)
#define GLB_STRNCPY(dest, destSize, src, count) strncpy_s(dest, destSize, src, count)
#define GLB_WSTRNCPY(dest, destSize, src, count) wcsncpy_s(dest, destSize, src, count)
#define GLB_STRCAT(dest, destSize, src) strcat_s(dest, destSize, src)
#define GLB_WSTRCAT(dest, destSize, src) wcscat_s(dest, destSize, src)
#define GLB_SPRINTF(format, formatSize, ...) sprintf_s(format, formatSize, __VA_ARGS__)
#define GLB_WSPRINTF(format, formatSize, ...) swprintf_s(format, formatSize, __VA_ARGS__)
#define GLB_VSNPRINTF(dest, destSize, format, args) vsnprintf_s(dest, destSize, _TRUNCATE, format, args)
#define GLB_VWSNPRINTF(dest, destSize, format, args) vswprintf_s(dest, destSize, format, args)
#define GLB_STRERROR(dest, destSize, errorNum) strerror_s(dest, destSize, errorNum)
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

template<typename DestT, typename SrcT>
inline DestT* CheckedWmemcpy(DestT* dest, const std::size_t destSize, const SrcT* src, const std::size_t srcSize)noexcept{
    if(srcSize == 0u)
        return dest;
    if(srcSize > destSize)
        std::abort();
    if(dest == nullptr || src == nullptr)
        std::abort();

    return std::wmemcpy(dest, src, srcSize);
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
inline int BoundedNCopy(CharT* dest, const std::size_t destSize, const CharT* src, const std::size_t count)noexcept{
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

#define GLB_STRNLEN(src, count) strnlen(src, count)
#define GLB_WSTRNLEN(src, count) wcsnlen(src, count)
#define GLB_MEMCPY(dest, destSize, src, srcSize) ::CompileDetail::CheckedMemcpy(dest, destSize, src, srcSize)
#define GLB_WMEMCPY(dest, destSize, src, srcSize) ::CompileDetail::CheckedWmemcpy(dest, destSize, src, srcSize)
#define GLB_STRCPY(dest, destSize, src) ::CompileDetail::BoundedCopy(dest, destSize, src)
#define GLB_WSTRCPY(dest, destSize, src) ::CompileDetail::BoundedCopy(dest, destSize, src)
#define GLB_STRNCPY(dest, destSize, src, count) ::CompileDetail::BoundedNCopy(dest, destSize, src, count)
#define GLB_WSTRNCPY(dest, destSize, src, count) ::CompileDetail::BoundedNCopy(dest, destSize, src, count)
#define GLB_STRCAT(dest, destSize, src) ::CompileDetail::BoundedCat(dest, destSize, src)
#define GLB_WSTRCAT(dest, destSize, src) ::CompileDetail::BoundedCat(dest, destSize, src)
#define GLB_SPRINTF(format, formatSize, ...) snprintf(format, formatSize, __VA_ARGS__)
#define GLB_WSPRINTF(format, formatSize, ...) swprintf(format, formatSize, __VA_ARGS__)
#define GLB_VSNPRINTF(dest, destSize, format, args) vsnprintf(dest, destSize, format, args)
#define GLB_VWSNPRINTF(dest, destSize, format, args) vswprintf(dest, destSize, format, args)
#define GLB_STRERROR(dest, destSize, errorNum) ::CompileDetail::BoundedStrError(dest, destSize, errorNum)
#endif

#if defined(UNICODE) || defined(_UNICODE)
#define GLB_TCOUT GLB_WCOUT
#define GLB_TCIN GLB_WCIN
#define GLB_TCERR GLB_WCERR
#define GLB_TSTRLEN(src) GLB_WSTRLEN(src)
#define GLB_TSTRNLEN(src, count) GLB_WSTRNLEN(src, count)
#define GLB_TSTRCMP(lhs, rhs) GLB_WSTRCMP(lhs, rhs)
#define GLB_TMEMCPY(dest, destSize, src, srcSize) GLB_WMEMCPY(dest, destSize, src, srcSize)
#define GLB_TSTRCPY(dest, destSize, src) GLB_WSTRCPY(dest, destSize, src)
#define GLB_TSTRNCPY(dest, destSize, src, count) GLB_WSTRNCPY(dest, destSize, src, count)
#define GLB_TSTRCAT(dest, destSize, src) GLB_WSTRCAT(dest, destSize, src)
#define GLB_TSPRINTF(format, formatSize, ...) GLB_WSPRINTF(format, formatSize, __VA_ARGS__)
#define GLB_TVSNPRINTF(dest, destSize, format, args) GLB_VWSNPRINTF(dest, destSize, format, args)
#else
#define GLB_TCOUT GLB_COUT
#define GLB_TCIN GLB_CIN
#define GLB_TCERR GLB_CERR
#define GLB_TSTRLEN(src) GLB_STRLEN(src)
#define GLB_TSTRNLEN(src, count) GLB_STRNLEN(src, count)
#define GLB_TSTRCMP(lhs, rhs) GLB_STRCMP(lhs, rhs)
#define GLB_TMEMCPY(dest, destSize, src, srcSize) GLB_MEMCPY(dest, destSize, src, srcSize)
#define GLB_TSTRCPY(dest, destSize, src) GLB_STRCPY(dest, destSize, src)
#define GLB_TSTRNCPY(dest, destSize, src, count) GLB_STRNCPY(dest, destSize, src, count)
#define GLB_TSTRCAT(dest, destSize, src) GLB_STRCAT(dest, destSize, src)
#define GLB_TSPRINTF(format, formatSize, ...) GLB_SPRINTF(format, formatSize, __VA_ARGS__)
#define GLB_TVSNPRINTF(dest, destSize, format, args) GLB_VSNPRINTF(dest, destSize, format, args)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr bool CanEnableDebugRuntime()noexcept{
#if !defined(GLB_FINAL)
    return true;
#else
    return false;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

