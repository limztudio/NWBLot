include("${PROJECT_SOURCE_DIR}/configuration/ImportedLibraries.cmake")

function(nwb_microhttpd_generate_config output_file)
    if(NOT WIN32)
        message(FATAL_ERROR "nwb_microhttpd_generate_config is only used for the vendored Windows build")
    endif()

    include(CheckCSourceCompiles)
    include(CheckIncludeFile)
    include(CheckSymbolExists)
    include(CheckTypeSize)

    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    list(APPEND CMAKE_REQUIRED_DEFINITIONS "-DWIN32_LEAN_AND_MEAN=1")
    list(APPEND CMAKE_REQUIRED_LIBRARIES ws2_32)

    set(WINDOWS 1)
    set(INLINE_FUNC 1)
    set(BAUTH_SUPPORT 1)
    set(DAUTH_SUPPORT 1)
    set(MHD_DAUTH_DEF_MAX_NC_ 1000)
    set(MHD_DAUTH_DEF_TIMEOUT_ 90)
    set(MHD_MD5_SUPPORT 1)
    set(MHD_SHA256_SUPPORT 1)
    set(MHD_SHA512_256_SUPPORT 1)
    set(HAVE_POSTPROCESSOR 1)
    set(HAVE_MESSAGES 1)
    set(UPGRADE_SUPPORT 1)
    set(COOKIE_SUPPORT 1)
    set(HAS_FD_SETSIZE_OVERRIDABLE 1)
    set(_MHD_ITC_SOCKETPAIR 1)
    set(MHD_USE_W32_THREADS 1)
    set(HAVE_POLL 1)
    set(HAVE_GETSOCKNAME 1)
    set(MHD_USE_GETSOCKNAME 1)
    set(_GNU_SOURCE 1)
    set(STDC_HEADERS 1)
    set(NWB_MHD_WIN32_WINNT "0x0600")

    if(MSVC)
        set(MSVC 1)
        set(NWB_MHD_STATIC_INLINE "static __forceinline")
    else()
        set(NWB_MHD_STATIC_INLINE "static inline")
    endif()

    check_c_source_compiles("
        int main(void)
        {
            int count = 4;
            int values[count];
            values[0] = 0;
            return values[0];
        }
    " HAVE_C_VARARRAYS)
    if(NOT HAVE_C_VARARRAYS)
        set(__STDC_NO_VLA__ 1)
    endif()

    check_c_source_compiles("
        int main(void)
        {
            return (int)__builtin_bswap32(1u);
        }
    " MHD_HAVE___BUILTIN_BSWAP32)
    check_c_source_compiles("
        int main(void)
        {
            return (int)__builtin_bswap64(1ull);
        }
    " MHD_HAVE___BUILTIN_BSWAP64)

    check_c_source_compiles("
        _Noreturn void nwb_mhd_never_returns(void)
        {
            for (;;) {}
        }
        int main(void) { return 0; }
    " NWB_MHD_HAVE_C_NORETURN)
    if(NWB_MHD_HAVE_C_NORETURN)
        set(NWB_MHD_NORETURN "_Noreturn")
    elseif(MSVC)
        set(NWB_MHD_NORETURN "__declspec(noreturn)")
    else()
        set(NWB_MHD_NORETURN "__attribute__((noreturn))")
    endif()

    check_type_size("size_t" SIZEOF_SIZE_T LANGUAGE C)
    check_type_size("int" SIZEOF_INT LANGUAGE C)
    check_type_size("unsigned int" SIZEOF_UNSIGNED_INT LANGUAGE C)
    check_type_size("unsigned long long" SIZEOF_UNSIGNED_LONG_LONG LANGUAGE C)
    check_type_size("long" SIZEOF_STRUCT_TIMEVAL_TV_SEC LANGUAGE C)
    set(CMAKE_EXTRA_INCLUDE_FILES "stdint.h")
    check_type_size("int64_t" SIZEOF_INT64_T LANGUAGE C)
    check_type_size("uint64_t" SIZEOF_UINT64_T LANGUAGE C)
    unset(CMAKE_EXTRA_INCLUDE_FILES)

    if(NOT SIZEOF_SIZE_T)
        set(SIZEOF_SIZE_T ${CMAKE_SIZEOF_VOID_P})
    endif()
    if(NOT SIZEOF_STRUCT_TIMEVAL_TV_SEC)
        set(SIZEOF_STRUCT_TIMEVAL_TV_SEC 4)
    endif()

    foreach(_nwb_mhd_header IN ITEMS
        assert.h
        errno.h
        fcntl.h
        inttypes.h
        limits.h
        locale.h
        math.h
        memory.h
        sdkddkver.h
        stdalign.h
        stdbool.h
        stddef.h
        stdint.h
        stdio.h
        stdlib.h
        string.h
        strings.h
        sys/stat.h
        sys/types.h
        time.h
        windows.h
        winsock2.h
        ws2tcpip.h
    )
        string(TOUPPER "${_nwb_mhd_header}" _nwb_mhd_header_var)
        string(REGEX REPLACE "[^A-Z0-9]" "_" _nwb_mhd_header_var "${_nwb_mhd_header_var}")
        check_include_file("${_nwb_mhd_header}" HAVE_${_nwb_mhd_header_var})
    endforeach()

    if(HAVE_ASSERT_H)
        set(HAVE_ASSERT 1)
    endif()
    if(HAVE_STDBOOL_H)
        set(HAVE_REAL_BOOL 1)
        set(HAVE_BUILTIN_TYPE_BOOL 1)
    endif()

    check_symbol_exists("_lseeki64" "io.h" HAVE___LSEEKI64)
    check_symbol_exists("calloc" "stdlib.h" HAVE_CALLOC)
    check_symbol_exists("gmtime_s" "time.h" HAVE_W32_GMTIME_S)
    check_symbol_exists("rand" "stdlib.h" HAVE_RAND)
    check_symbol_exists("snprintf" "stdio.h" HAVE_SNPRINTF)

    check_c_source_compiles("
        #include <winsock2.h>
        #include <ws2tcpip.h>
        int main(void)
        {
            struct sockaddr_in6 addr;
            addr.sin6_family = AF_INET6;
            return addr.sin6_family == AF_INET6 ? 0 : 1;
        }
    " HAVE_INET6)
    check_c_source_compiles("
        int main(void)
        {
            const char *name = __func__;
            return name[0] == 0;
        }
    " HAVE___FUNC__)
    check_c_source_compiles("
        int main(void)
        {
            return (int)_Alignof(int);
        }
    " HAVE_C_ALIGNOF)

    configure_file("${CMAKE_CURRENT_LIST_DIR}/MHD_config.h.in" "${output_file}" @ONLY)
endfunction()

if(WIN32)
    set(_nwb_microhttpd_config_header "${CMAKE_CURRENT_BINARY_DIR}/MHD_config.h")
    nwb_microhttpd_generate_config("${_nwb_microhttpd_config_header}")

    add_library(nwb_vendor_microhttpd STATIC)
    add_library(nwb::microhttpd ALIAS nwb_vendor_microhttpd)
    target_sources(nwb_vendor_microhttpd PRIVATE
        "${_nwb_microhttpd_config_header}"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/basicauth.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/connection.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/daemon.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/digestauth.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/gen_auth.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/internal.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/md5.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/memorypool.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/mhd_mono_clock.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/mhd_panic.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/mhd_send.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/mhd_sockets.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/mhd_str.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/mhd_threads.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/postprocessor.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/reason_phrase.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/response.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/sha256.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/sha512_256.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/sysfdsetsize.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/tsearch.c"
    )
    target_include_directories(nwb_vendor_microhttpd PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${CMAKE_CURRENT_LIST_DIR}"
    )
    target_include_directories(nwb_vendor_microhttpd SYSTEM PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/include"
    )
    target_compile_definitions(nwb_vendor_microhttpd
        PRIVATE
            _LIB
            MHD_W32LIB
            _CRT_SECURE_NO_DEPRECATE
            WIN32_LEAN_AND_MEAN=1
    )
    nwb_disable_vendor_warnings(nwb_vendor_microhttpd)
    nwb_apply_debug_symbols(nwb_vendor_microhttpd)
    if(NWB_COMPILER_FRONTEND_MSVC)
        target_compile_options(nwb_vendor_microhttpd PRIVATE /wd4127 /wd5287)
    endif()
    target_link_libraries(nwb_vendor_microhttpd PUBLIC ws2_32)
else()
    nwb_find_library_target(nwb_microhttpd
        INCLUDE_NAME microhttpd.h
        LIB_NAMES microhttpd
    )
    if(TARGET nwb_microhttpd)
        add_library(nwb::microhttpd ALIAS nwb_microhttpd)
    else()
        message(STATUS "Log server support disabled: libmicrohttpd development files not found")
    endif()
endif()
