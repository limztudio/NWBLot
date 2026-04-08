include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/VulkanInclude.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/WaylandInclude.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/X11Include.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ShadercInclude.cmake")

function(nwb_configure_external_dependencies)
    nwb_configure_vulkan_include()
    nwb_configure_wayland_include()
    nwb_configure_x11_include()
    nwb_configure_shaderc_include()
endfunction()
