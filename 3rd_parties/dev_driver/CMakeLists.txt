if(TARGET nwb::dev_driver_headers)
    return()
endif()

add_library(nwb_dev_driver_headers INTERFACE)
add_library(nwb::dev_driver_headers ALIAS nwb_dev_driver_headers)
target_include_directories(nwb_dev_driver_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/.."
    "${CMAKE_CURRENT_LIST_DIR}/include")
