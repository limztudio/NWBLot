include("${PROJECT_SOURCE_DIR}/configuration/ImportedLibraries.cmake")

# --------------------------------------------------------------------------- #
# MHD_config.h generation.
#
# Upstream libmicrohttpd expects a configure-generated MHD_config.h (it is pulled in
# unconditionally by include/mhd_options.h). The NWB vendored tree ships no autotools, so
# this function probes the platform directly with CMake's check_* modules and configures
# the MHD_config.h.in template. It works for both the vendored Windows build (which fills
# the W32 macros) and the vendored Linux/POSIX build (which fills the POSIX macros); the
# .in template's #cmakedefine directives simply evaluate to undefined for whichever
# platform half does not apply.
# --------------------------------------------------------------------------- #
function(nwb_microhttpd_generate_config output_file)
    include(CheckCSourceCompiles)
    include(CheckIncludeFile)
    include(CheckSymbolExists)
    include(CheckFunctionExists)
    include(CheckTypeSize)

    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

    # Feature toggles common to both vendored builds.
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
    set(STDC_HEADERS 1)
    set(HAVE_INET6 1)
    set(_GNU_SOURCE 1)

    if(WIN32)
        # --- Windows platform flags ---------------------------------------------------------
        list(APPEND CMAKE_REQUIRED_DEFINITIONS "-DWIN32_LEAN_AND_MEAN=1")
        list(APPEND CMAKE_REQUIRED_LIBRARIES ws2_32)

        set(WINDOWS 1)
        set(INLINE_FUNC 1)
        set(HAS_FD_SETSIZE_OVERRIDABLE 1)
        set(_MHD_ITC_SOCKETPAIR 1)
        set(MHD_USE_W32_THREADS 1)
        set(HAVE_POLL 1)
        set(HAVE_GETSOCKNAME 1)
        set(MHD_USE_GETSOCKNAME 1)
        set(NWB_MHD_WIN32_WINNT "0x0600")

        if(MSVC)
            set(MSVC 1)
            set(NWB_MHD_STATIC_INLINE "static __forceinline")
        else()
            set(NWB_MHD_STATIC_INLINE "static inline")
        endif()

        # Windows-only header set.
        set(_nwb_mhd_headers
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

        # Windows-only symbol probes.
        check_symbol_exists("_lseeki64" "io.h" HAVE___LSEEKI64)
    else()
        # --- POSIX (Linux) platform flags ---------------------------------------------------
        set(LINUX 1)
        set(NWB_MHD_STATIC_INLINE "static inline")

        # Use the optimal GNU/Linux inter-thread-comm primitive: eventfd. mhd_itc.h selects
        # _MHD_ITC_EVENTFD over _MHD_ITC_PIPE when both are available, so only define one.
        check_include_file("sys/eventfd.h" HAVE_SYS_EVENTFD_H)
        if(HAVE_SYS_EVENTFD_H)
            set(_MHD_ITC_EVENTFD 1)
        else()
            set(_MHD_ITC_PIPE 1)
        endif()

        set(MHD_USE_POSIX_THREADS 1)

        # POSIX header set (the W32 set above is skipped here).
        set(_nwb_mhd_headers
            arpa/inet.h
            assert.h
            endian.h
            errno.h
            fcntl.h
            inttypes.h
            limits.h
            locale.h
            math.h
            memory.h
            net/if.h
            netdb.h
            netinet/in.h
            netinet/in_systm.h
            netinet/tcp.h
            poll.h
            pthread.h
            signal.h
            stdalign.h
            stdbool.h
            stddef.h
            stdint.h
            stdio.h
            stdlib.h
            string.h
            strings.h
            sys/eventfd.h
            sys/ioctl.h
            sys/mman.h
            sys/msg.h
            sys/param.h
            sys/select.h
            sys/socket.h
            sys/stat.h
            sys/time.h
            sys/types.h
            sys/uio.h
            time.h
            unistd.h
        )

        # POSIX function probes (headers passed where the prototype needs one).
        check_function_exists("accept4" HAVE_ACCEPT4)
        check_function_exists("clock_gettime" HAVE_CLOCK_GETTIME)
        check_function_exists("fork" HAVE_FORK)
        check_function_exists("gettimeofday" HAVE_GETTIMEOFDAY)
        check_function_exists("gmtime_r" HAVE_GMTIME_R)
        check_function_exists("pipe2" HAVE_PIPE2_FUNC)
        check_function_exists("poll" HAVE_POLL)
        check_function_exists("pread" HAVE_PREAD)
        check_function_exists("pread64" HAVE_PREAD64)
        check_function_exists("random" HAVE_RANDOM)
        check_function_exists("sendfile" HAVE_SENDFILE)
        check_function_exists("sendfile64" HAVE_SENDFILE64)
        check_function_exists("sendmsg" HAVE_SENDMSG)
        check_function_exists("sysconf" HAVE_SYSCONF)
        check_function_exists("timespec_get" HAVE_TIMESPEC_GET)
        check_function_exists("usleep" HAVE_USLEEP)
        check_function_exists("waitpid" HAVE_WAITPID)
        check_function_exists("writev" HAVE_WRITEV)
        check_function_exists("lseek64" HAVE_LSEEK64)
        check_function_exists("nanosleep" HAVE_NANOSLEEP)
        check_function_exists("epoll_create1" HAVE_EPOLL_CREATE1)
        check_function_exists("pthread_sigmask" HAVE_PTHREAD_SIGMASK)

        check_symbol_exists("SOCK_NONBLOCK" "sys/socket.h" HAVE_SOCK_NONBLOCK)
        check_symbol_exists("getsockname" "sys/socket.h" HAVE_GETSOCKNAME)

        # Linux sendfile path (mhd_options.h derives MHD_LINUX_SOLARIS_SENDFILE / _MHD_HAVE_SENDFILE from it).
        if(HAVE_SENDFILE)
            set(HAVE_LINUX_SENDFILE 1)
        endif()

        # GNU pthread_setname_np(const char*) thread-name support.
        check_c_source_compiles("
            #define _GNU_SOURCE 1
            #include <pthread.h>
            int main(void)
            {
              pthread_t t = 0;
              return pthread_setname_np (t, \"name\");
            }
        " HAVE_PTHREAD_SETNAME_NP_GNU)

        check_c_source_compiles("
            #define _GNU_SOURCE 1
            #include <time.h>
            int main(void)
            {
              struct tm tm_v;
              struct tm *r = gmtime_s (&(time_t){0}, &tm_v);
              return r == 0;
            }
        " HAVE_C11_GMTIME_S)
    endif()

    # --- Probes shared by both platforms ---------------------------------------------------
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
    elseif(WIN32 AND MSVC)
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
        set(SIZEOF_STRUCT_TIMEVAL_TV_SEC 8)
    endif()

    foreach(_nwb_mhd_header IN LISTS _nwb_mhd_headers)
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

    check_function_exists("rand" HAVE_RAND)
    check_function_exists("snprintf" HAVE_SNPRINTF)

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
    # __attribute__((format)) support for the MHD_FUNC pretty-function macro (mhd_str.h).
    check_c_source_compiles("
        __attribute__((format(printf, 1, 2)))
        static void nwb_mhd_chk(const char *fmt, ...) { (void) fmt; }
        int main(void) { nwb_mhd_chk(\"%d\", 1); return 0; }
    " HAVE_MHD_FUNC_)

    configure_file("${CMAKE_CURRENT_LIST_DIR}/MHD_config.h.in" "${output_file}" @ONLY)
endfunction()

# --------------------------------------------------------------------------- #
# The non-test library sources common to both vendored builds. connection_https.c
# is intentionally excluded: it unconditionally includes <gnutls/gnutls.h> and the
# NWB build never defines HTTPS_SUPPORT, so the logserver runs plain HTTP only.
# On POSIX the pipe/eventfd ITC lives in mhd_itc.c and the snprintf/calloc shims in
# mhd_compat.c (no-ops when the libc already provides them).
# --------------------------------------------------------------------------- #
set(_nwb_microhttpd_sources
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

set(_nwb_microhttpd_config_header "${CMAKE_CURRENT_BINARY_DIR}/MHD_config.h")
nwb_microhttpd_generate_config("${_nwb_microhttpd_config_header}")

add_library(nwb_vendor_microhttpd STATIC)
add_library(nwb::microhttpd ALIAS nwb_vendor_microhttpd)
target_sources(nwb_vendor_microhttpd PRIVATE
    "${_nwb_microhttpd_config_header}"
    ${_nwb_microhttpd_sources}
)
target_include_directories(nwb_vendor_microhttpd PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}"
)
target_include_directories(nwb_vendor_microhttpd SYSTEM PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include"
)

if(WIN32)
    target_compile_definitions(nwb_vendor_microhttpd
        PRIVATE
            _LIB
            MHD_W32LIB
            _CRT_SECURE_NO_DEPRECATE
            WIN32_LEAN_AND_MEAN=1
    )
    nwb_apply_debug_symbols(nwb_vendor_microhttpd)
    if(NWB_COMPILER_FRONTEND_MSVC)
        target_compile_options(nwb_vendor_microhttpd PRIVATE /wd4127 /wd5287)
    endif()
    target_link_libraries(nwb_vendor_microhttpd PUBLIC ws2_32)
else()
    # POSIX: eventfd ITC, the pipe ITC, and POSIX threads all live in this translation unit set.
    target_sources(nwb_vendor_microhttpd PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/mhd_itc.c"
        "${CMAKE_CURRENT_LIST_DIR}/microhttpd/mhd_compat.c"
    )
    # _GNU_SOURCE enables accept4/pipe2/sendfile64/pthread_setname_np prototypes; the config already
    # defines it, but propagate it as a compile flag too so every TU sees it before any libc header.
    target_compile_definitions(nwb_vendor_microhttpd PRIVATE _GNU_SOURCE)
    target_link_libraries(nwb_vendor_microhttpd PUBLIC pthread)
endif()

nwb_disable_vendor_warnings(nwb_vendor_microhttpd)
