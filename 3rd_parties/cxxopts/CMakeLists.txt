if(TARGET nwb::cxxopts_headers)
    return()
endif()

add_library(nwb_cxxopts_headers INTERFACE)
add_library(nwb::cxxopts_headers ALIAS nwb_cxxopts_headers)
target_include_directories(nwb_cxxopts_headers SYSTEM INTERFACE "${CMAKE_CURRENT_LIST_DIR}")
