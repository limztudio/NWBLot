nwb_declare_vendor_headers(nwb_pevents_headers nwb::pevents_headers "${CMAKE_CURRENT_LIST_DIR}/src")
target_include_directories(nwb_pevents_headers SYSTEM INTERFACE "${CMAKE_CURRENT_LIST_DIR}/..")
