if(TARGET nwb::comgr_headers)
    return()
endif()

add_library(nwb_comgr_headers INTERFACE)
add_library(nwb::comgr_headers ALIAS nwb_comgr_headers)
target_include_directories(nwb_comgr_headers SYSTEM INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")
