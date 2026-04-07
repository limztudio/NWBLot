if(TARGET nwb::tsl_headers)
    return()
endif()

add_library(nwb_tsl_headers INTERFACE)
add_library(nwb::tsl_headers ALIAS nwb_tsl_headers)
target_include_directories(nwb_tsl_headers INTERFACE "${CMAKE_CURRENT_LIST_DIR}")
