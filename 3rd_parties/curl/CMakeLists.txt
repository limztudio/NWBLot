include("${PROJECT_SOURCE_DIR}/configuration/ImportedLibraries.cmake")

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
    set(_nwb_curl_makefile_inc "${CMAKE_CURRENT_BINARY_DIR}/nwb-curl-Makefile.inc.cmake")
    nwb_curl_transform_makefile_inc("${CMAKE_CURRENT_LIST_DIR}/lib/Makefile.inc" "${_nwb_curl_makefile_inc}")
    include("${_nwb_curl_makefile_inc}")

    nwb_curl_resolve_makefile_paths(_nwb_curl_sources "${CMAKE_CURRENT_LIST_DIR}/lib" ${CSOURCES} ${HHEADERS})
    list(PREPEND _nwb_curl_sources "${CMAKE_CURRENT_LIST_DIR}/curl_config.h")

    add_library(nwb_vendor_curl STATIC)
    add_library(nwb::curl ALIAS nwb_vendor_curl)
    target_sources(nwb_vendor_curl PRIVATE ${_nwb_curl_sources})

    target_include_directories(nwb_vendor_curl SYSTEM PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}"
    )
    target_include_directories(nwb_vendor_curl PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib")
    target_compile_definitions(nwb_vendor_curl
        PRIVATE
            CURL_DISABLE_HSTS
            CURL_HIDDEN_SYMBOLS
            HAVE_CONFIG_H
            BUILDING_LIBCURL
            USE_IPV6
            USE_THREADS_WIN32
            USE_WIN32_LARGE_FILES
            USE_WINDOWS_SSPI
            _CRT_SECURE_NO_DEPRECATE
            _CRT_NONSTDC_NO_DEPRECATE
        PUBLIC
            CURL_STATICLIB
    )
    nwb_disable_vendor_warnings(nwb_vendor_curl)
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
