include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/ImportedLibraries.cmake")

function(nwb_configure_shaderc_include)
    if(TARGET nwb::shaderc_combined)
        return()
    endif()

    find_path(_shaderc_include_dir
        NAMES shaderc/shaderc.hpp
        HINTS "$ENV{VULKAN_SDK}"
        PATH_SUFFIXES include Include
    )

    if(WIN32)
        find_library(_shaderc_dbg
            NAMES shaderc_combinedd
            HINTS "$ENV{VULKAN_SDK}"
            PATH_SUFFIXES lib Lib lib64
        )
        find_library(_shaderc_rel
            NAMES shaderc_combined
            HINTS "$ENV{VULKAN_SDK}"
            PATH_SUFFIXES lib Lib lib64
        )
    else()
        find_library(_shaderc_library
            NAMES shaderc_combined shaderc_shared shaderc
            HINTS "$ENV{VULKAN_SDK}"
            PATH_SUFFIXES lib Lib lib64
        )

        if(NOT _shaderc_include_dir OR NOT _shaderc_library)
            message(STATUS "Shader cook targets disabled: missing shaderc headers or library")
            return()
        endif()
    endif()

    if(WIN32 AND NOT _shaderc_include_dir)
        message(STATUS "Shader cook targets disabled: missing shaderc headers or library")
        return()
    endif()
    if(WIN32 AND (NOT _shaderc_dbg OR NOT _shaderc_rel))
        message(STATUS "Shader cook targets disabled: missing shaderc headers or library")
        return()
    endif()

    add_library(nwb_shaderc_combined UNKNOWN IMPORTED GLOBAL)
    if(WIN32)
        nwb_set_imported_library_locations(nwb_shaderc_combined
            DEFAULT "${_shaderc_rel}"
            DBG "${_shaderc_dbg}"
            OPT "${_shaderc_rel}"
            FIN "${_shaderc_rel}"
        )
    else()
        nwb_set_imported_library_locations(nwb_shaderc_combined DEFAULT "${_shaderc_library}")
    endif()

    set_target_properties(nwb_shaderc_combined PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_shaderc_include_dir}"
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_shaderc_include_dir}"
    )

    add_library(nwb::shaderc_combined ALIAS nwb_shaderc_combined)
endfunction()
