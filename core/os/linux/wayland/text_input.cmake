function(nwb_enable_text_input_protocol target_name)
    find_file(_text_input_xml
        NAMES text-input-unstable-v3.xml
        HINTS "$ENV{WAYLAND_PROTOCOLS_DIR}"
        PATHS /usr/share/wayland-protocols /usr/local/share/wayland-protocols
        PATH_SUFFIXES unstable/text-input
    )
    if(NOT _text_input_xml)
        message(STATUS "Wayland text input v3 unavailable: keyboard commits remain available")
        return()
    endif()

    get_target_property(_scanner nwb_wayland NWB_WAYLAND_SCANNER)
    if(NOT _scanner)
        message(FATAL_ERROR "Wayland text input protocol generation requires wayland-scanner")
    endif()
    set(_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/${target_name}_text_input")
    set(_header "${_generated_dir}/text-input-v3-client-protocol.h")
    set(_code "${_generated_dir}/text-input-v3-protocol.c")
    add_custom_command(
        OUTPUT "${_header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_generated_dir}"
        COMMAND "${_scanner}" client-header "${_text_input_xml}" "${_header}"
        DEPENDS "${_text_input_xml}"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${_code}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_generated_dir}"
        COMMAND "${_scanner}" private-code "${_text_input_xml}" "${_code}"
        DEPENDS "${_text_input_xml}" "${_header}"
        VERBATIM
    )
    target_sources(${target_name} PRIVATE "${_header}" "${_code}")
    target_include_directories(${target_name} PRIVATE "${_generated_dir}")
    target_compile_definitions(${target_name} PRIVATE NWB_OS_WITH_TEXT_INPUT_V3=1)
endfunction()
