if(TARGET nwb::wmi)
    return()
endif()

if(WIN32)
    add_library(nwb_vendor_wmi STATIC
        "${CMAKE_CURRENT_LIST_DIR}/src/wmi.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/src/wmiresult.cpp")
    add_library(nwb::wmi ALIAS nwb_vendor_wmi)
    target_include_directories(nwb_vendor_wmi SYSTEM PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
    target_link_libraries(nwb_vendor_wmi PUBLIC ole32 oleaut32 wbemuuid)
    set_target_properties(nwb_vendor_wmi PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON)
    nwb_disable_vendor_warnings(nwb_vendor_wmi)
    nwb_apply_debug_symbols(nwb_vendor_wmi)
else()
    add_library(nwb_wmi_headers INTERFACE)
    add_library(nwb::wmi ALIAS nwb_wmi_headers)
    target_include_directories(nwb_wmi_headers SYSTEM INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")
endif()
