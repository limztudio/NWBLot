include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/CodeGen.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/BasicInclude.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/SimdAVX2.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/UnicodeChar.cmake")

function(nwb_apply_internal_target_defaults target)
    nwb_apply_codegen(${target})
    nwb_apply_basic_include(${target})

    target_link_libraries(${target} PUBLIC
        nwb::tsl_headers
        nwb_internal_headers
    )

    if(WIN32)
        nwb_apply_unicode_char(${target})
    endif()

    nwb_apply_simd_avx2(${target})
endfunction()

function(nwb_declare_static_library target)
    add_library(${target} STATIC)
    if(target MATCHES "^nwb_(.+)$")
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${CMAKE_MATCH_1}")
    endif()
    nwb_apply_internal_target_defaults(${target})
endfunction()

function(nwb_declare_interface_library target)
    add_library(${target} INTERFACE)
    target_link_libraries(${target} INTERFACE
        nwb::tsl_headers
        nwb_internal_headers
    )

    if(WIN32)
        target_compile_definitions(${target} INTERFACE UNICODE _UNICODE)
    endif()
endfunction()

function(nwb_declare_executable target)
    add_executable(${target})
    if(target MATCHES "^nwb_(.+)$")
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${CMAKE_MATCH_1}")
    endif()
    nwb_apply_internal_target_defaults(${target})
    if(WIN32)
        target_link_libraries(${target} PRIVATE shell32)
    endif()
endfunction()

function(nwb_target_link_libraries_whole_archive target)
    foreach(library IN LISTS ARGN)
        if(MSVC)
            target_link_libraries(${target} PRIVATE ${library})
            target_link_options(${target} PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:${library}>")
        elseif(APPLE)
            target_link_libraries(${target} PRIVATE
                "-Wl,-force_load,$<TARGET_FILE:${library}>"
                ${library}
            )
        else()
            target_link_libraries(${target} PRIVATE
                "-Wl,--whole-archive"
                "$<TARGET_FILE:${library}>"
                "-Wl,--no-whole-archive"
                ${library}
            )
        endif()
    endforeach()
endfunction()

function(nwb_target_sources_standalone_runtime target)
    target_sources(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/core/alloc/standalone_runtime.cpp"
        "${PROJECT_SOURCE_DIR}/core/alloc/standalone_runtime.h"
    )
endfunction()
