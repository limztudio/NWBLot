if(TARGET nwb::cli11_headers)
    return()
endif()

add_library(nwb_cli11_headers INTERFACE)
add_library(nwb::cli11_headers ALIAS nwb_cli11_headers)
target_include_directories(nwb_cli11_headers INTERFACE "${CMAKE_CURRENT_LIST_DIR}")
