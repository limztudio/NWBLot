include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/ImportedLibraries.cmake")

function(nwb_configure_vulkan_include)
    if(TARGET nwb::volk)
        return()
    endif()

    find_path(_nwb_volk_include_dir
        NAMES volk/volk.h
        HINTS "$ENV{VULKAN_SDK}"
        PATH_SUFFIXES include Include
    )
    find_library(_nwb_volk_library
        NAMES volk libvolk.so.1 libvolk
        HINTS "$ENV{VULKAN_SDK}"
        PATH_SUFFIXES lib Lib lib64
    )

    if(NOT _nwb_volk_include_dir OR NOT _nwb_volk_library)
        message(FATAL_ERROR
            "Failed to locate volk. Install volk/Vulkan development files or set VULKAN_SDK so volk/volk.h and the volk library are discoverable."
        )
    endif()

    add_library(nwb_volk_raw UNKNOWN IMPORTED GLOBAL)
    nwb_set_imported_library_locations(nwb_volk_raw DEFAULT "${_nwb_volk_library}")
    set_target_properties(nwb_volk_raw PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_nwb_volk_include_dir}"
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_nwb_volk_include_dir}"
    )

    add_library(nwb::volk ALIAS nwb_volk_raw)
endfunction()
