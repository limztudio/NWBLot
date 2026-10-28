include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/CodeGen.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/BasicInclude.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/SimdAVX2.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/UnicodeChar.cmake")

function(nwb_apply_internal_target_defaults target)
    nwb_apply_codegen(${target})
    nwb_apply_basic_include(${target})

    target_link_libraries(${target} PUBLIC nwb::tsl_headers nwb_internal_headers)

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
    target_link_libraries(${target} INTERFACE nwb::tsl_headers nwb_internal_headers)

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
        target_link_libraries(${target} PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,${library}>")
        # Force whole-archive even when a transitive link names this library normally.
        set_property(TARGET ${target} PROPERTY "LINK_LIBRARY_OVERRIDE_${library}" WHOLE_ARCHIVE)
    endforeach()
endfunction()

function(nwb_register_pipeline_volume_entries)
    foreach(library IN LISTS ARGN)
        set_property(GLOBAL APPEND PROPERTY NWB_PIPELINE_VOLUME_ENTRY_TARGETS "${library}")
        if(TARGET nwb_dependency_computer)
            nwb_target_link_libraries_whole_archive(nwb_dependency_computer "${library}")
        endif()
        if(TARGET nwb_asset_builder)
            nwb_target_link_libraries_whole_archive(nwb_asset_builder "${library}")
        endif()
    endforeach()
endfunction()

function(nwb_target_sources_standalone_runtime target)
    target_sources(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/core/alloc/standalone_runtime.cpp"
        "${PROJECT_SOURCE_DIR}/core/alloc/standalone_runtime.h"
    )
endfunction()

function(nwb_target_adhd_sources target_name)
    cmake_parse_arguments(PARSE_ARGV 1 ADHD "" "NAMESPACE" "SOURCES;INCLUDES;DEFINES;DEPENDS")

    if(DEFINED ADHD_UNPARSED_ARGUMENTS AND NOT "${ADHD_UNPARSED_ARGUMENTS}" STREQUAL "")
        message(FATAL_ERROR "nwb_target_adhd_sources: unparsed arguments '${ADHD_UNPARSED_ARGUMENTS}'; pass files under SOURCES")
    endif()

    set(adhd_sources ${ADHD_SOURCES})

    set(out_root "${CMAKE_BINARY_DIR}/adhd_gen/${target_name}")
    file(MAKE_DIRECTORY "${out_root}")
    target_include_directories(${target_name} PUBLIC "${out_root}")

    set(extra_args "")
    if(DEFINED ADHD_NAMESPACE AND NOT "${ADHD_NAMESPACE}" STREQUAL "")
        list(APPEND extra_args --namespace "${ADHD_NAMESPACE}")
    endif()
    foreach(inc_dir IN LISTS ADHD_INCLUDES)
        list(APPEND extra_args -I "${inc_dir}")
    endforeach()
    foreach(def_val IN LISTS ADHD_DEFINES)
        list(APPEND extra_args -D "${def_val}")
    endforeach()

    set(gen_headers "")
    foreach(adhd_file IN LISTS adhd_sources)
        get_filename_component(adhd_abs "${adhd_file}" ABSOLUTE)
        get_filename_component(adhd_stem "${adhd_file}" NAME_WLE)
        get_filename_component(adhd_abs_dir "${adhd_abs}" DIRECTORY)
        file(RELATIVE_PATH adhd_rel_dir "${PROJECT_SOURCE_DIR}" "${adhd_abs_dir}")
        if(adhd_rel_dir MATCHES "^\\.\\." OR adhd_rel_dir STREQUAL "")
            set(out_dir "${out_root}")
        else()
            set(out_dir "${out_root}/${adhd_rel_dir}")
        endif()
        file(MAKE_DIRECTORY "${out_dir}")
        target_include_directories(${target_name} PUBLIC "${out_dir}")
        set(gen_h "${out_dir}/${adhd_stem}.adhd.h")
        set(gen_cpp "${out_dir}/${adhd_stem}.adhd.cpp")

        add_custom_command(
            OUTPUT "${gen_h}" "${gen_cpp}"
            COMMAND $<TARGET_FILE:nwb_adhd_compiler> --input "${adhd_abs}" -o "${out_dir}" -I "${CMAKE_CURRENT_LIST_DIR}" -I "${PROJECT_SOURCE_DIR}" ${extra_args}
            DEPENDS nwb_adhd_compiler "${adhd_abs}" ${ADHD_DEPENDS}
            COMMENT "Compiling adhd source ${adhd_file}"
            VERBATIM
        )
        target_sources(${target_name} PUBLIC "$<BUILD_INTERFACE:${gen_h}>" PRIVATE "${gen_cpp}")
        list(APPEND gen_headers "${gen_h}")
    endforeach()

    if(gen_headers)
        add_custom_target(${target_name}_adhd_gen DEPENDS ${gen_headers})
        add_dependencies(${target_name} ${target_name}_adhd_gen)
    endif()
endfunction()
