include_guard(GLOBAL)

function(nwb_configure_wayland_include)
    if(TARGET nwb::wayland)
        return()
    endif()

    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        return()
    endif()

    if(NOT NWB_ENABLE_WAYLAND)
        message(STATUS "Wayland backend disabled by NWB_ENABLE_WAYLAND=OFF")
        return()
    endif()

    find_program(_wayland_scanner
        NAMES wayland-scanner
    )
    find_path(_wayland_include_dir
        NAMES wayland-client.h
        PATH_SUFFIXES include Include
    )
    find_library(_wayland_client_library
        NAMES wayland-client
        PATH_SUFFIXES lib Lib lib64
    )

    find_path(_xkbcommon_include_dir
        NAMES xkbcommon/xkbcommon.h
        PATH_SUFFIXES include Include
    )
    find_library(_xkbcommon_library
        NAMES xkbcommon
        PATH_SUFFIXES lib Lib lib64
    )

    find_file(_wayland_xdg_shell_xml
        NAMES xdg-shell.xml
        HINTS "$ENV{WAYLAND_PROTOCOLS_DIR}"
        PATHS
            /usr/share/wayland-protocols
            /usr/local/share/wayland-protocols
        PATH_SUFFIXES
            stable/xdg-shell
    )

    if(NOT _wayland_scanner OR NOT _wayland_include_dir OR NOT _wayland_client_library OR NOT _xkbcommon_include_dir OR NOT _xkbcommon_library OR NOT _wayland_xdg_shell_xml)
        message(STATUS "Wayland backend disabled: missing wayland-client/xkbcommon/wayland-protocols tooling")
        return()
    endif()

    add_library(nwb_wayland INTERFACE)
    target_include_directories(nwb_wayland SYSTEM INTERFACE
        "${_wayland_include_dir}"
        "${_xkbcommon_include_dir}"
    )
    target_link_libraries(nwb_wayland INTERFACE
        "${_wayland_client_library}"
        "${_xkbcommon_library}"
    )
    set_target_properties(nwb_wayland PROPERTIES
        NWB_WAYLAND_SCANNER "${_wayland_scanner}"
        NWB_WAYLAND_XDG_SHELL_XML "${_wayland_xdg_shell_xml}"
    )

    add_library(nwb::wayland ALIAS nwb_wayland)

    message(STATUS "Wayland backend enabled")
endfunction()

function(nwb_enable_wayland_protocol target_name)
    if(NOT TARGET nwb::wayland)
        return()
    endif()

    get_target_property(_wayland_scanner nwb_wayland NWB_WAYLAND_SCANNER)
    get_target_property(_wayland_xdg_shell_xml nwb_wayland NWB_WAYLAND_XDG_SHELL_XML)
    if(NOT _wayland_scanner OR NOT _wayland_xdg_shell_xml)
        message(FATAL_ERROR "Wayland protocol generation requested without a configured nwb::wayland target")
    endif()

    set(_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/${target_name}_wayland")
    set(_xdg_shell_header "${_generated_dir}/xdg-shell-client-protocol.h")
    set(_xdg_shell_code "${_generated_dir}/xdg-shell-protocol.c")

    add_custom_command(
        OUTPUT "${_xdg_shell_header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_generated_dir}"
        COMMAND "${_wayland_scanner}" client-header "${_wayland_xdg_shell_xml}" "${_xdg_shell_header}"
        DEPENDS "${_wayland_xdg_shell_xml}"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${_xdg_shell_code}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_generated_dir}"
        COMMAND "${_wayland_scanner}" private-code "${_wayland_xdg_shell_xml}" "${_xdg_shell_code}"
        DEPENDS "${_wayland_xdg_shell_xml}" "${_xdg_shell_header}"
        VERBATIM
    )

    target_sources(${target_name} PRIVATE
        "${_xdg_shell_code}"
        "${_xdg_shell_header}"
    )
    target_include_directories(${target_name} PRIVATE "${_generated_dir}")
endfunction()
