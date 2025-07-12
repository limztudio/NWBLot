// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
#define NWB_INLINE
#elif defined(NWB_OPTIMIZE) || defined(NWB_FINAL)
#if defined(_MSC_VER)
#define NWB_INLINE __forceinline
#else
#define NWB_INLINE inline
#endif
#endif

#if defined(_MSC_VER)
#define NWB_VECTORCALL __vectorcall
#else
#define NWB_VECTORCALL
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_DEBUG)
#if defined(_MSC_VER)
#define NWB_HARDBREAK __debugbreak()
#define NWB_SOFTBREAK __debugbreak()
#else
#define NWB_HARDBREAK
#define NWB_SOFTBREAK
#endif
#elif defined(NWB_OPTIMIZE)
#define NWB_HARDBREAK
#if defined(_MSC_VER)
#define NWB_SOFTBREAK __debugbreak()
#else
#define NWB_SOFTBREAK
#endif
#else
#define NWB_HARDBREAK
#define NWB_SOFTBREAK
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_COUT std::cout
#define NWB_WCOUT std::wcout
#define NWB_CERR std::cerr
#define NWB_WCERR std::wcerr
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
#else
#define NWB_STRNLEN(src, count) strnlen(src, count)
#define NWB_WSTRNLEN(src, count) wcsnlen(src, count)
#define NWB_MEMCMP(lhs, rhs, size) memcmp(lhs, rhs, size)
#define NWB_MEMCPY(dest, destSize, src, srcSize) memcpy(dest, src, srcSize)
#define NWB_WMEMCPY(dest, destSize, src, srcSize) wmemcpy(dest, src, srcSize)
#define NWB_STRCPY(dest, destSize, src) strcpy(dest, src)
#define NWB_WSTRCPY(dest, destSize, src) wcscpy(dest, src)
#define NWB_STRNCPY(dest, destSize, src, count) strncpy(dest, src, count)
#define NWB_WSTRNCPY(dest, destSize, src, count) wcsncpy(dest, src, count)
#define NWB_STRCAT(dest, destSize, src) strcat(dest, src)
#define NWB_WSTRCAT(dest, destSize, src) wcscat(dest, src)
#define NWB_SPRINTF(format, formatSize, ...) sprintf(format, __VA_ARGS__)
#define NWB_WSPRINTF(format, formatSize, ...) swprintf(format, __VA_ARGS__)
#endif

#if defined(UNICODE) || defined(_UNICODE)
#define NWB_TCOUT NWB_WCOUT
#define NWB_TCERR NWB_WCERR
#define NWB_TSTRLEN(src) NWB_WSTRLEN(src)
#define NWB_TSTRNLEN(src, count) NWB_WSTRNLEN(src, count)
#define NWB_TSTRCMP(lhs, rhs) NWB_WSTRCMP(lhs, rhs)
#define NWB_TMEMCPY(dest, destSize, src, srcSize) NWB_WMEMCPY(dest, destSize, src, srcSize)
#define NWB_TSTRCPY(dest, destSize, src) NWB_WSTRCPY(dest, destSize, src)
#define NWB_TSTRNCPY(dest, destSize, src, count) NWB_WSTRNCPY(dest, destSize, src, count)
#define NWB_TSTRCAT(dest, destSize, src) NWB_WSTRCAT(dest, destSize, src)
#define NWB_TSPRINTF(format, formatSize, ...) NWB_WSPRINTF(format, formatSize, __VA_ARGS__)
#else
#define NWB_TCOUT NWB_COUT
#define NWB_TCERR NWB_CERR
#define NWB_TSTRLEN(src) NWB_STRLEN(src)
#define NWB_TSTRNLEN(src, count) NWB_STRNLEN(src, count)
#define NWB_TSTRCMP(lhs, rhs) NWB_STRCMP(lhs, rhs)
#define NWB_TMEMCPY(dest, destSize, src, srcSize) NWB_MEMCPY(dest, destSize, src, srcSize)
#define NWB_TSTRCPY(dest, destSize, src) NWB_STRCPY(dest, destSize, src)
#define NWB_TSTRNCPY(dest, destSize, src, count) NWB_STRNCPY(dest, destSize, src, count)
#define NWB_TSTRCAT(dest, destSize, src) NWB_STRCAT(dest, destSize, src)
#define NWB_TSPRINTF(format, formatSize, ...) NWB_SPRINTF(format, formatSize, __VA_ARGS__)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

