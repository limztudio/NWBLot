add_library(nwb_vendor_volk STATIC
    "${CMAKE_CURRENT_LIST_DIR}/volk/volk.c"
    "${CMAKE_CURRENT_LIST_DIR}/volk/volk.h"
)
add_library(nwb::volk ALIAS nwb_vendor_volk)

target_include_directories(nwb_vendor_volk SYSTEM PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}"
)

target_link_libraries(nwb_vendor_volk PUBLIC nwb::vulkan_headers)

if(WIN32)
    target_compile_definitions(nwb_vendor_volk PUBLIC VK_USE_PLATFORM_WIN32_KHR)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(nwb_vendor_volk PUBLIC VK_USE_PLATFORM_XLIB_KHR)
    if(NWB_ENABLE_WAYLAND)
        target_compile_definitions(nwb_vendor_volk PUBLIC VK_USE_PLATFORM_WAYLAND_KHR)
    endif()
endif()

if(NOT WIN32)
    target_link_libraries(nwb_vendor_volk PUBLIC ${CMAKE_DL_LIBS})
endif()

nwb_disable_vendor_warnings(nwb_vendor_volk)
nwb_apply_debug_symbols(nwb_vendor_volk)
