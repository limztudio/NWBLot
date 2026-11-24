if(TARGET nwb::dev_driver_headers)
    return()
endif()

nwb_declare_vendor_headers(nwb_dev_driver_headers nwb::dev_driver_headers "${CMAKE_CURRENT_LIST_DIR}/..")
target_include_directories(nwb_dev_driver_headers SYSTEM INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")
