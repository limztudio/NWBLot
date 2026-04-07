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
        IMPORTED_CONFIGURATIONS "DBG;OPT;FIN"
        IMPORTED_LOCATION "${ARG_DEFAULT}"
        IMPORTED_LOCATION_DBG "${ARG_DBG}"
        IMPORTED_LOCATION_OPT "${ARG_OPT}"
        IMPORTED_LOCATION_FIN "${ARG_FIN}"
    )
endfunction()

function(nwb_find_library_target target_name)
    set(options REQUIRED)
    set(oneValueArgs INCLUDE_NAME)
    set(multiValueArgs LIB_NAMES HINTS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    find_path(_include_dir
        NAMES "${ARG_INCLUDE_NAME}"
        HINTS ${ARG_HINTS}
        PATH_SUFFIXES include Include
    )

    find_library(_library
        NAMES ${ARG_LIB_NAMES}
        HINTS ${ARG_HINTS}
        PATH_SUFFIXES lib Lib lib64
    )

    if(ARG_REQUIRED AND (NOT _include_dir OR NOT _library))
        message(FATAL_ERROR "Failed to locate dependency for ${target_name}")
    endif()

    if(_include_dir AND _library AND NOT TARGET ${target_name})
        add_library(${target_name} UNKNOWN IMPORTED GLOBAL)
        nwb_set_imported_library_locations(${target_name} DEFAULT "${_library}")
        set_target_properties(${target_name} PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${_include_dir}"
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_include_dir}"
        )
    endif()
endfunction()
