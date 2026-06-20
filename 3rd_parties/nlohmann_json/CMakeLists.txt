add_library(nwb_vendor_nlohmann_json INTERFACE)
add_library(nwb::nlohmann_json ALIAS nwb_vendor_nlohmann_json)

target_include_directories(nwb_vendor_nlohmann_json SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}"
)
