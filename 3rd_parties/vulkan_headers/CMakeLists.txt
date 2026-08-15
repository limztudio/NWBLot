add_library(nwb_vulkan_headers INTERFACE)
add_library(nwb::vulkan_headers ALIAS nwb_vulkan_headers)

target_include_directories(nwb_vulkan_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}"
)
