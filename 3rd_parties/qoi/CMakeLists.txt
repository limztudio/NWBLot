if(TARGET nwb::qoi)
    return()
endif()

add_library(nwb_vendor_qoi INTERFACE)
add_library(nwb::qoi ALIAS nwb_vendor_qoi)
target_include_directories(nwb_vendor_qoi SYSTEM INTERFACE "${CMAKE_CURRENT_LIST_DIR}")
