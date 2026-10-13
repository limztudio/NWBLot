if(TARGET nwb::tinyxml2)
    return()
endif()

nwb_declare_vendor_headers(nwb_vendor_tinyxml2 nwb::tinyxml2 "${CMAKE_CURRENT_LIST_DIR}")
target_sources(nwb_vendor_tinyxml2 INTERFACE "${CMAKE_CURRENT_LIST_DIR}/tinyxml2.cpp")
