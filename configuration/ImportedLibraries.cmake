include_guard(GLOBAL)

include(CMakeParseArguments)

function(nwb_set_imported_library_locations target_name)
    set(options)
    set(oneValueArgs DEFAULT DBG OPT FIN)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "" ${ARGN})

    if(NOT ARG_DEFAULT)
        message(FATAL_ERROR "nwb_set_imported_library_locations requires DEFAULT")
    endif()

    if(NOT ARG_DBG)
        set(ARG_DBG "${ARG_DEFAULT}")
    endif()
    if(NOT ARG_OPT)
        set(ARG_OPT "${ARG_DEFAULT}")
    endif()
    if(NOT ARG_FIN)
        set(ARG_FIN "${ARG_DEFAULT}")
    endif()

    set_target_properties(${target_name} PROPERTIES
        IMPORTED_CONFIGURATIONS "${NWB_IMPORTED_CONFIGURATIONS}"
        IMPORTED_LOCATION "${ARG_DEFAULT}"
        IMPORTED_LOCATION_DBG "${ARG_DBG}"
        IMPORTED_LOCATION_OPT "${ARG_OPT}"
        IMPORTED_LOCATION_FIN "${ARG_FIN}"
    )
endfunction()
