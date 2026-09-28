function(nwb_enable_primary_selection_protocol target_name)
    find_file(_primary_selection_xml
        NAMES primary-selection-unstable-v1.xml
        HINTS "$ENV{WAYLAND_PROTOCOLS_DIR}"
        PATHS /usr/share/wayland-protocols /usr/local/share/wayland-protocols
        PATH_SUFFIXES unstable/primary-selection
    )
    if(NOT _primary_selection_xml)
        message(STATUS "Wayland clipboard primary selection disabled: protocol XML unavailable")
        return()
    endif()

    get_target_property(_scanner nwb_wayland NWB_WAYLAND_SCANNER)
    if(NOT _scanner)
        message(FATAL_ERROR "Wayland clipboard protocol generation requires wayland-scanner")
    endif()
    set(_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/${target_name}_primary_selection")
    set(_header "${_generated_dir}/primary-selection-client-protocol.h")
    set(_code "${_generated_dir}/primary-selection-protocol.c")
    add_custom_command(
        OUTPUT "${_header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_generated_dir}"
        COMMAND "${_scanner}" client-header "${_primary_selection_xml}" "${_header}"
        DEPENDS "${_primary_selection_xml}"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${_code}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_generated_dir}"
        COMMAND "${_scanner}" private-code "${_primary_selection_xml}" "${_code}"
        DEPENDS "${_primary_selection_xml}" "${_header}"
        VERBATIM
    )
    target_sources(${target_name} PRIVATE "${_header}" "${_code}")
    target_include_directories(${target_name} PRIVATE "${_generated_dir}")
    target_compile_definitions(${target_name} PRIVATE NWB_OS_WITH_PRIMARY_SELECTION=1)
endfunction()
