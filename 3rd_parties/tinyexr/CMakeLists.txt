if(TARGET nwb::tinyexr)
    return()
endif()

add_library(nwb_vendor_tinyexr INTERFACE)
add_library(nwb::tinyexr ALIAS nwb_vendor_tinyexr)
target_include_directories(nwb_vendor_tinyexr SYSTEM INTERFACE "${CMAKE_CURRENT_LIST_DIR}")
