include_guard(GLOBAL)

macro(nwb_require_x64)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "NWBLot currently supports x64/64-bit builds only.")
    endif()

    set(NWB_OUTPUT_ARCH "x64")
endmacro()

macro(nwb_configure_general_output)
    foreach(_type RUNTIME LIBRARY ARCHIVE)
        foreach(_cfg dbg opt fin)
            string(TOUPPER "${_cfg}" _cfg_upper)
            set(CMAKE_${_type}_OUTPUT_DIRECTORY_${_cfg_upper} "${PROJECT_SOURCE_DIR}/__exec/${NWB_OUTPUT_ARCH}/${_cfg}")
        endforeach()
    endforeach()
endmacro()
