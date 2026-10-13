if(TARGET nwb::tsl_headers)
    return()
endif()

nwb_declare_vendor_headers(nwb_tsl_headers nwb::tsl_headers "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(nwb_tsl_headers INTERFACE
    _SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING
)
