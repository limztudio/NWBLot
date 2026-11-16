if(TARGET nwb::siphash)
    return()
endif()

nwb_declare_vendor_headers(nwb_vendor_siphash nwb::siphash "${CMAKE_CURRENT_LIST_DIR}/include")
