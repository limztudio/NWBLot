include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/ImportedLibraries.cmake")

function(nwb_configure_vulkan_include)
    if(TARGET nwb::volk)
        return()
    endif()

    nwb_find_library_target(nwb_volk_raw REQUIRED
        INCLUDE_NAME volk/volk.h
        LIB_NAMES volk
        HINTS "$ENV{VULKAN_SDK}"
    )

    add_library(nwb::volk ALIAS nwb_volk_raw)
endfunction()
