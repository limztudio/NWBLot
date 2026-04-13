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
