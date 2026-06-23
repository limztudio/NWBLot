if(TARGET nwb::tinyxml2)
    return()
endif()

add_library(nwb_vendor_tinyxml2 INTERFACE)
add_library(nwb::tinyxml2 ALIAS nwb_vendor_tinyxml2)
target_include_directories(nwb_vendor_tinyxml2 SYSTEM INTERFACE "${CMAKE_CURRENT_LIST_DIR}")
target_sources(nwb_vendor_tinyxml2 INTERFACE "${CMAKE_CURRENT_LIST_DIR}/tinyxml2.cpp")
