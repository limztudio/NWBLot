if(TARGET nwb::tinydds)
    return()
endif()

add_library(nwb_vendor_tinydds INTERFACE)
add_library(nwb::tinydds ALIAS nwb_vendor_tinydds)
target_include_directories(nwb_vendor_tinydds SYSTEM INTERFACE "${CMAKE_CURRENT_LIST_DIR}")
