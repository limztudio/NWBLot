if(TARGET nwb::libclang)
    return()
endif()

nwb_declare_vendor_headers(nwb_libclang_headers nwb::libclang_headers "${CMAKE_CURRENT_LIST_DIR}/include")

option(NWB_LIBCLANG_STATIC "Treat the resolved libclang library as a standalone static archive." OFF)
set(NWB_LIBCLANG_LIBRARY "" CACHE FILEPATH "Optional path to libclang static archive, import library, or shared library.")
set(NWB_LIBCLANG_RUNTIME_LIBRARY "" CACHE FILEPATH "Optional path to the matching libclang shared runtime library.")
set(NWB_LIBCLANG_STATIC_DEPENDENCIES "" CACHE STRING "Optional extra libraries required when linking a static libclang archive.")

set(_nwb_libclang_link_lib "")
set(_nwb_libclang_runtime "")
set(_nwb_libclang_bundled FALSE)
set(_nwb_libclang_is_static ${NWB_LIBCLANG_STATIC})

if(WIN32)
    set(_nwb_bundled_static_a "${CMAKE_CURRENT_LIST_DIR}/lib/windows/${NWB_OUTPUT_ARCH}/libclang.a")
    set(_nwb_bundled_lib "${CMAKE_CURRENT_LIST_DIR}/lib/windows/${NWB_OUTPUT_ARCH}/libclang.lib")
    set(_nwb_bundled_dll "${CMAKE_CURRENT_LIST_DIR}/lib/windows/${NWB_OUTPUT_ARCH}/libclang.dll")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_nwb_bundled_static_a "${CMAKE_CURRENT_LIST_DIR}/lib/linux/${NWB_OUTPUT_ARCH}/libclang.a")
    set(_nwb_bundled_lib "${CMAKE_CURRENT_LIST_DIR}/lib/linux/${NWB_OUTPUT_ARCH}/libclang.so")
    set(_nwb_bundled_dll "")
else()
    set(_nwb_bundled_static_a "")
    set(_nwb_bundled_lib "")
    set(_nwb_bundled_dll "")
endif()

if(NWB_LIBCLANG_LIBRARY AND EXISTS "${NWB_LIBCLANG_LIBRARY}")
    set(_nwb_libclang_link_lib "${NWB_LIBCLANG_LIBRARY}")
elseif(_nwb_bundled_static_a AND EXISTS "${_nwb_bundled_static_a}")
    set(_nwb_libclang_link_lib "${_nwb_bundled_static_a}")
    set(_nwb_libclang_is_static TRUE)
elseif(_nwb_bundled_lib AND EXISTS "${_nwb_bundled_lib}")
    set(_nwb_libclang_link_lib "${_nwb_bundled_lib}")
    set(_nwb_libclang_bundled TRUE)
    if(_nwb_bundled_dll AND EXISTS "${_nwb_bundled_dll}")
        set(_nwb_libclang_runtime "${_nwb_bundled_dll}")
    elseif(WIN32 AND NOT NWB_LIBCLANG_RUNTIME_LIBRARY)
        set(_nwb_libclang_is_static TRUE)
    endif()
elseif(CMAKE_CXX_COMPILER)
    get_filename_component(_nwb_cxx_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_nwb_cxx_root "${_nwb_cxx_dir}" DIRECTORY)
    if(WIN32)
        foreach(_nwb_candidate
            "${_nwb_cxx_root}/lib/libclang.lib"
            "${_nwb_cxx_dir}/libclang.lib"
        )
            if(EXISTS "${_nwb_candidate}")
                set(_nwb_libclang_link_lib "${_nwb_candidate}")
                break()
            endif()
        endforeach()
        foreach(_nwb_candidate
            "${_nwb_cxx_dir}/libclang.dll"
            "${_nwb_cxx_root}/bin/libclang.dll"
            "${_nwb_cxx_root}/lib/libclang.dll"
        )
            if(EXISTS "${_nwb_candidate}")
                set(_nwb_libclang_runtime "${_nwb_candidate}")
                break()
            endif()
        endforeach()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_nwb_libclang_search_dirs "${_nwb_cxx_dir}" "${_nwb_cxx_root}/lib" "${_nwb_cxx_root}/lib64")
        find_program(_nwb_llvm_config NAMES llvm-config HINTS "${_nwb_cxx_dir}" NO_CACHE)
        if(_nwb_llvm_config)
            execute_process(
                COMMAND "${_nwb_llvm_config}" --libdir
                RESULT_VARIABLE _nwb_llvm_libdir_res
                OUTPUT_VARIABLE _nwb_llvm_libdir
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(_nwb_llvm_libdir_res EQUAL 0 AND _nwb_llvm_libdir)
                list(PREPEND _nwb_libclang_search_dirs "${_nwb_llvm_libdir}")
            endif()
        endif()
        if(CMAKE_CXX_COMPILER_VERSION MATCHES "^([0-9]+)")
            list(APPEND _nwb_libclang_search_dirs "/usr/lib/llvm-${CMAKE_MATCH_1}/lib")
        endif()
        list(APPEND _nwb_libclang_search_dirs "/usr/lib" "/usr/lib64")
        foreach(_nwb_dir IN LISTS _nwb_libclang_search_dirs)
            if(EXISTS "${_nwb_dir}/libclang.a")
                set(_nwb_libclang_link_lib "${_nwb_dir}/libclang.a")
                set(_nwb_libclang_is_static TRUE)
                break()
            endif()
            file(GLOB _nwb_linux_libclang LIST_DIRECTORIES false "${_nwb_dir}/libclang.so*")
            if(_nwb_linux_libclang)
                list(SORT _nwb_linux_libclang)
                list(GET _nwb_linux_libclang 0 _nwb_libclang_link_lib)
                set(_nwb_libclang_runtime "${_nwb_libclang_link_lib}")
                break()
            endif()
        endforeach()
    endif()
endif()

if(NWB_LIBCLANG_RUNTIME_LIBRARY AND EXISTS "${NWB_LIBCLANG_RUNTIME_LIBRARY}")
    set(_nwb_libclang_runtime "${NWB_LIBCLANG_RUNTIME_LIBRARY}")
endif()

if(NOT _nwb_libclang_link_lib)
    find_library(_nwb_found_libclang NAMES clang libclang)
    if(_nwb_found_libclang)
        set(_nwb_libclang_link_lib "${_nwb_found_libclang}")
    endif()
endif()

if(NOT _nwb_libclang_link_lib)
    message(FATAL_ERROR "libclang library not found. Place libclang under 3rd_parties/libclang/lib/ or set NWB_LIBCLANG_LIBRARY.")
endif()

if(_nwb_libclang_link_lib MATCHES "\\.(a|lib)$" AND NOT _nwb_libclang_runtime)
    set(_nwb_libclang_is_static TRUE)
endif()

if(_nwb_libclang_is_static)
    add_library(nwb_libclang STATIC IMPORTED GLOBAL)
    set_target_properties(nwb_libclang PROPERTIES
        IMPORTED_LOCATION "${_nwb_libclang_link_lib}"
        INTERFACE_COMPILE_DEFINITIONS "CINDEX_NO_EXPORTS"
    )
elseif(WIN32)
    add_library(nwb_libclang SHARED IMPORTED GLOBAL)
    set_target_properties(nwb_libclang PROPERTIES
        IMPORTED_IMPLIB "${_nwb_libclang_link_lib}"
    )
    if(_nwb_libclang_runtime)
        set_target_properties(nwb_libclang PROPERTIES
            IMPORTED_LOCATION "${_nwb_libclang_runtime}"
        )
    endif()
else()
    add_library(nwb_libclang SHARED IMPORTED GLOBAL)
    set_target_properties(nwb_libclang PROPERTIES
        IMPORTED_LOCATION "${_nwb_libclang_link_lib}"
    )
endif()

set(_nwb_libclang_iface_deps nwb::libclang_headers)
if(NOT WIN32)
    list(APPEND _nwb_libclang_iface_deps ${CMAKE_DL_LIBS})
endif()
if(NWB_LIBCLANG_STATIC_DEPENDENCIES)
    list(APPEND _nwb_libclang_iface_deps ${NWB_LIBCLANG_STATIC_DEPENDENCIES})
endif()
set_target_properties(nwb_libclang PROPERTIES
    INTERFACE_LINK_LIBRARIES "${_nwb_libclang_iface_deps}"
)
add_library(nwb::libclang ALIAS nwb_libclang)

if(_nwb_libclang_runtime)
    file(TO_CMAKE_PATH "${_nwb_libclang_runtime}" _nwb_libclang_hint_normalized)
else()
    file(TO_CMAKE_PATH "${_nwb_libclang_link_lib}" _nwb_libclang_hint_normalized)
endif()
set_property(GLOBAL PROPERTY NWB_LIBCLANG_RUNTIME "${_nwb_libclang_hint_normalized}")
set_property(GLOBAL PROPERTY NWB_LIBCLANG_RUNTIME_BUNDLED "${_nwb_libclang_bundled}")

# Expose the discovered libclang hint path for Clang builtin resource-dir lookup and copy bundled runtimes if present.
function(nwb_apply_libclang_runtime target)
    get_property(_runtime GLOBAL PROPERTY NWB_LIBCLANG_RUNTIME)
    get_property(_bundled GLOBAL PROPERTY NWB_LIBCLANG_RUNTIME_BUNDLED)
    if(NOT _runtime)
        return()
    endif()

    target_compile_definitions(${target} PRIVATE "NWB_LIBCLANG_DEFAULT_RUNTIME_PATH=\"${_runtime}\"")

    if(_bundled)
        get_target_property(_target_type ${target} TYPE)
        if(_target_type STREQUAL "EXECUTABLE" OR _target_type STREQUAL "SHARED_LIBRARY" OR _target_type STREQUAL "MODULE_LIBRARY")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_runtime}" "$<TARGET_FILE_DIR:${target}>"
                COMMENT "Copying libclang runtime next to ${target}"
                VERBATIM
            )
            if(NOT WIN32)
                set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
            endif()
        endif()
    endif()
endfunction()
