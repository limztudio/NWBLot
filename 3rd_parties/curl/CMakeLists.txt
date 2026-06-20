include("${PROJECT_SOURCE_DIR}/configuration/ImportedLibraries.cmake")

macro(nwb_curl_prefill_type_size type_name type_size)
    set(HAVE_SIZEOF_${type_name} TRUE)
    set(SIZEOF_${type_name} ${type_size})
    set(SIZEOF_${type_name}_CODE "#define SIZEOF_${type_name} ${type_size}")
endmacro()

function(nwb_curl_generate_config output_file)
    if(NOT WIN32)
        message(FATAL_ERROR "nwb_curl_generate_config is only used for the vendored Windows build")
    endif()

    include(CheckTypeSize)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

    if(CMAKE_C_COMPILER_TARGET)
        set(CURL_OS "\"${CMAKE_C_COMPILER_TARGET}\"")
    else()
        set(CURL_OS "\"${CMAKE_SYSTEM_NAME}\"")
    endif()

    set(CURL_DISABLE_HSTS 1)
    set(STDC_HEADERS 1)
    set(USE_IPV6 1)
    set(USE_RESOLV_THREADED 1)
    set(USE_UNIX_SOCKETS 1)

    if(NOT WINDOWS_STORE)
        set(HAVE_LDAP_SSL 1)
        set(USE_WIN32_CRYPTO 1)
        set(USE_WIN32_LDAP 1)
        set(USE_WINDOWS_SSPI 1)
    endif()

    if(MINGW)
        set(HAVE_BASENAME 1)
        set(HAVE_BOOL_T 1)
        set(HAVE_DIRENT_H 1)
        set(HAVE_GETTIMEOFDAY 1)
        set(HAVE_LIBGEN_H 1)
        set(HAVE_OPENDIR 1)
        set(HAVE_STDBOOL_H 1)
        set(HAVE_STDDEF_H 1)
        set(HAVE_STDINT_H 1)
        set(HAVE_STRINGS_H 1)
        set(HAVE_SYS_PARAM_H 1)
        set(HAVE_UNISTD_H 1)
        set(HAVE_UTIME_H 1)
        set(ssize_t "")
    else()
        set(HAVE_DIRENT_H 0)
        set(HAVE_GETTIMEOFDAY 0)
        set(HAVE_LIBGEN_H 0)
        set(HAVE_OPENDIR 0)
        set(HAVE_STRINGS_H 0)
        set(HAVE_SYS_PARAM_H 0)
        set(HAVE_UNISTD_H 0)
        set(HAVE_UTIME_H 0)
        set(HAVE_STDDEF_H 1)
        set(HAVE_STDINT_H 1)
        if(MSVC_VERSION GREATER_EQUAL 1800)
            set(HAVE_STDBOOL_H 1)
            set(HAVE_BOOL_T 1)
        endif()
        set(HAVE_BASENAME 0)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(ssize_t "__int64")
        else()
            set(ssize_t "int")
        endif()
    endif()

    if((CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 4.9) OR
       (CMAKE_C_COMPILER_ID STREQUAL "Clang" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 3.6))
        set(HAVE_STDATOMIC_H 1)
        set(HAVE_ATOMIC 1)
    endif()

    set(HAVE_ACCEPT4 0)
    set(HAVE_ALARM 0)
    set(HAVE_ARC4RANDOM 0)
    set(HAVE_ARPA_INET_H 0)
    set(HAVE_CLOSESOCKET 1)
    set(HAVE_EVENTFD 0)
    set(HAVE_FCNTL 0)
    set(HAVE_FCNTL_H 1)
    set(HAVE_FCNTL_O_NONBLOCK 0)
    set(HAVE_FNMATCH 0)
    set(HAVE_FREEADDRINFO 1)
    set(HAVE_FSETXATTR 0)
    set(HAVE_GETADDRINFO 1)
    set(HAVE_GETADDRINFO_THREADSAFE 1)
    set(HAVE_GETEUID 0)
    set(HAVE_GETHOSTBYNAME_R 0)
    set(HAVE_GETHOSTBYNAME_R_3 0)
    set(HAVE_GETHOSTBYNAME_R_5 0)
    set(HAVE_GETHOSTBYNAME_R_6 0)
    set(HAVE_GETHOSTNAME 1)
    set(HAVE_GETIFADDRS 0)
    set(HAVE_GETPASS_R 0)
    set(HAVE_GETPEERNAME 1)
    set(HAVE_GETPPID 0)
    set(HAVE_GETPWUID 0)
    set(HAVE_GETPWUID_R 0)
    set(HAVE_GETRLIMIT 0)
    set(HAVE_GETSOCKNAME 1)
    set(HAVE_GLIBC_STRERROR_R 0)
    set(HAVE_GMTIME_R 0)
    set(HAVE_IFADDRS_H 0)
    set(HAVE_INET_NTOP 0)
    set(HAVE_INET_PTON 0)
    set(HAVE_IOCTLSOCKET 1)
    set(HAVE_IOCTLSOCKET_CAMEL 0)
    set(HAVE_IOCTLSOCKET_CAMEL_FIONBIO 0)
    set(HAVE_IOCTLSOCKET_FIONBIO 1)
    set(HAVE_IOCTL_FIONBIO 0)
    set(HAVE_IOCTL_SIOCGIFADDR 0)
    set(HAVE_IO_H 1)
    set(HAVE_LINUX_TCP_H 0)
    set(HAVE_LOCALE_H 1)
    set(HAVE_LOCALTIME_R 0)
    set(HAVE_MEMRCHR 0)
    set(HAVE_NETDB_H 0)
    set(HAVE_NETINET_IN6_H 0)
    set(HAVE_NETINET_IN_H 0)
    set(HAVE_NETINET_TCP_H 0)
    set(HAVE_NETINET_UDP_H 0)
    set(HAVE_NET_IF_H 0)
    set(HAVE_PIPE 0)
    set(HAVE_PIPE2 0)
    set(HAVE_POLL 0)
    set(HAVE_POLL_H 0)
    set(HAVE_POSIX_STRERROR_R 0)
    set(HAVE_PWD_H 0)
    set(HAVE_RECV 1)
    set(HAVE_SELECT 1)
    set(HAVE_SEND 1)
    set(HAVE_SENDMMSG 0)
    set(HAVE_SENDMSG 0)
    set(HAVE_SETLOCALE 1)
    set(HAVE_SETRLIMIT 0)
    set(HAVE_SETSOCKOPT_SO_NONBLOCK 0)
    set(HAVE_SIGACTION 0)
    set(HAVE_SIGINTERRUPT 0)
    set(HAVE_SIGNAL 1)
    set(HAVE_SIGSETJMP 0)
    set(HAVE_SOCKADDR_IN6_SIN6_SCOPE_ID 1)
    set(HAVE_SOCKET 1)
    set(HAVE_SOCKETPAIR 0)
    set(HAVE_STRERROR_R 0)
    set(HAVE_STROPTS_H 0)
    set(HAVE_STRUCT_SOCKADDR_STORAGE 1)
    set(HAVE_STRUCT_TIMEVAL 1)
    set(HAVE_SYS_EVENTFD_H 0)
    set(HAVE_SYS_FILIO_H 0)
    set(HAVE_SYS_IOCTL_H 0)
    set(HAVE_SYS_POLL_H 0)
    set(HAVE_SYS_RESOURCE_H 0)
    set(HAVE_SYS_SELECT_H 0)
    set(HAVE_SYS_SOCKIO_H 0)
    set(HAVE_SYS_TYPES_H 1)
    set(HAVE_SYS_UN_H 0)
    set(HAVE_SYS_UTIME_H 1)
    set(HAVE_TERMIOS_H 0)
    set(HAVE_TERMIO_H 0)
    set(HAVE_TIME_T_UNSIGNED 0)
    set(HAVE_UTIME 1)
    set(HAVE_UTIMES 0)

    nwb_curl_prefill_type_size("INT" 4)
    nwb_curl_prefill_type_size("LONG" 4)
    nwb_curl_prefill_type_size("__INT64" 8)
    nwb_curl_prefill_type_size("CURL_OFF_T" 8)
    nwb_curl_prefill_type_size("CURL_SOCKET_T" ${CMAKE_SIZEOF_VOID_P})
    nwb_curl_prefill_type_size("SIZE_T" ${CMAKE_SIZEOF_VOID_P})

    if(MSVC)
        nwb_curl_prefill_type_size("OFF_T" 4)
    else()
        set(_FILE_OFFSET_BITS 64)
        nwb_curl_prefill_type_size("OFF_T" 8)
    endif()

    set(CMAKE_EXTRA_INCLUDE_FILES "time.h")
    check_type_size("time_t" SIZEOF_TIME_T LANGUAGE C)
    unset(CMAKE_EXTRA_INCLUDE_FILES)

    if(NOT HAVE_SIZEOF_TIME_T)
        if(MSVC OR CMAKE_SIZEOF_VOID_P EQUAL 8)
            nwb_curl_prefill_type_size("TIME_T" 8)
        else()
            nwb_curl_prefill_type_size("TIME_T" 4)
        endif()
    endif()

    configure_file("${CMAKE_CURRENT_LIST_DIR}/lib/curl_config-cmake.h.in" "${output_file}")
endfunction()

function(nwb_curl_transform_makefile_inc input_file output_file)
    file(READ "${input_file}" _makefile_inc_text)
    string(REPLACE "$(top_srcdir)" "\${CMAKE_CURRENT_LIST_DIR}" _makefile_inc_text "${_makefile_inc_text}")
    string(REPLACE "$(top_builddir)" "\${CMAKE_CURRENT_BINARY_DIR}" _makefile_inc_text "${_makefile_inc_text}")

    string(REGEX REPLACE "\\\\\n" "!^!^!" _makefile_inc_text "${_makefile_inc_text}")
    string(REGEX REPLACE "([a-zA-Z_][a-zA-Z0-9_]*)[\t ]*=[\t ]*([^\n]*)" "set(\\1 \\2)" _makefile_inc_text "${_makefile_inc_text}")
    string(REPLACE "!^!^!" "\n" _makefile_inc_text "${_makefile_inc_text}")

    string(REGEX REPLACE "\\$\\(([a-zA-Z_][a-zA-Z0-9_]*)\\)" "\${\\1}" _makefile_inc_text "${_makefile_inc_text}")
    string(REGEX REPLACE "@([a-zA-Z_][a-zA-Z0-9_]*)@" "\${\\1}" _makefile_inc_text "${_makefile_inc_text}")

    file(WRITE "${output_file}" "${_makefile_inc_text}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input_file}")
endfunction()

function(nwb_curl_resolve_makefile_paths output_var base_dir)
    set(_resolved_paths)
    foreach(_item IN LISTS ARGN)
        if(IS_ABSOLUTE "${_item}")
            list(APPEND _resolved_paths "${_item}")
        else()
            list(APPEND _resolved_paths "${base_dir}/${_item}")
        endif()
    endforeach()
    set(${output_var} "${_resolved_paths}" PARENT_SCOPE)
endfunction()

if(WIN32)
    set(_nwb_curl_config_header "${CMAKE_CURRENT_BINARY_DIR}/curl_config.h")
    nwb_curl_generate_config("${_nwb_curl_config_header}")

    set(_nwb_curl_makefile_inc "${CMAKE_CURRENT_BINARY_DIR}/nwb-curl-Makefile.inc.cmake")
    nwb_curl_transform_makefile_inc("${CMAKE_CURRENT_LIST_DIR}/lib/Makefile.inc" "${_nwb_curl_makefile_inc}")
    include("${_nwb_curl_makefile_inc}")

    nwb_curl_resolve_makefile_paths(_nwb_curl_sources "${CMAKE_CURRENT_LIST_DIR}/lib" ${CSOURCES} ${HHEADERS})
    list(PREPEND _nwb_curl_sources "${_nwb_curl_config_header}")

    add_library(nwb_vendor_curl STATIC)
    add_library(nwb::curl ALIAS nwb_vendor_curl)
    target_sources(nwb_vendor_curl PRIVATE ${_nwb_curl_sources})

    target_include_directories(nwb_vendor_curl SYSTEM PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}"
    )
    target_include_directories(nwb_vendor_curl PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${CMAKE_CURRENT_LIST_DIR}/lib"
    )
    target_compile_definitions(nwb_vendor_curl
        PRIVATE
            CURL_HIDDEN_SYMBOLS
            HAVE_CONFIG_H
            BUILDING_LIBCURL
            USE_WIN32_LARGE_FILES
            _CRT_SECURE_NO_DEPRECATE
            _CRT_NONSTDC_NO_DEPRECATE
        PUBLIC
            CURL_STATICLIB
    )
    nwb_disable_vendor_warnings(nwb_vendor_curl)
    nwb_apply_debug_symbols(nwb_vendor_curl)
    target_link_libraries(nwb_vendor_curl PUBLIC
        ws2_32
        iphlpapi
        bcrypt
        secur32
        wldap32
        advapi32
        crypt32
        kernel32
        user32
        gdi32
        winspool
        shell32
        ole32
        oleaut32
        uuid
        comdlg32
    )
else()
    find_package(CURL QUIET)

    add_library(nwb_curl INTERFACE)
    add_library(nwb::curl ALIAS nwb_curl)

    if(TARGET CURL::libcurl)
        target_link_libraries(nwb_curl INTERFACE CURL::libcurl)
    else()
        find_path(_nwb_curl_include_dir
            NAMES curl/curl.h
            HINTS "${CMAKE_CURRENT_LIST_DIR}"
        )
        find_library(_nwb_curl_library
            NAMES curl libcurl.so.4 libcurl
            HINTS /usr /usr/local /
            PATH_SUFFIXES lib Lib lib64
        )

        if(NOT _nwb_curl_include_dir OR NOT _nwb_curl_library)
            message(FATAL_ERROR
                "Failed to locate CURL. Install libcurl development files or provide a compatible libcurl shared library plus headers."
            )
        endif()

        add_library(nwb_curl_raw UNKNOWN IMPORTED GLOBAL)
        nwb_set_imported_library_locations(nwb_curl_raw DEFAULT "${_nwb_curl_library}")
        set_target_properties(nwb_curl_raw PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${_nwb_curl_include_dir}"
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_nwb_curl_include_dir}"
        )
        target_link_libraries(nwb_curl INTERFACE nwb_curl_raw)
    endif()
endif()
