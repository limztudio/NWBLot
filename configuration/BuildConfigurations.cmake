include_guard(GLOBAL)

set(NWB_BUILD_CONFIGURATIONS dbg opt fin)
set(NWB_IMPORTED_CONFIGURATIONS DBG OPT FIN)

macro(nwb_configure_build_configs)
    if(CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_CONFIGURATION_TYPES "${NWB_BUILD_CONFIGURATIONS}" CACHE STRING "" FORCE)
    else()
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS ${NWB_BUILD_CONFIGURATIONS})
        if(NOT CMAKE_BUILD_TYPE)
            set(CMAKE_BUILD_TYPE "dbg" CACHE STRING "" FORCE)
        endif()
    endif()

    set(CMAKE_MAP_IMPORTED_CONFIG_DBG "DBG;Debug;RelWithDebInfo;Release;")
    set(CMAKE_MAP_IMPORTED_CONFIG_OPT "OPT;Release;RelWithDebInfo;MinSizeRel;")
    set(CMAKE_MAP_IMPORTED_CONFIG_FIN "FIN;Release;RelWithDebInfo;MinSizeRel;")
endmacro()
