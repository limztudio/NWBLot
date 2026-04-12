nwb_declare_static_library(nwb_graphics)
target_sources(nwb_graphics PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/basic.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/common.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/graphics.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/shader_archive.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/shader_cook.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_allocator.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_buffer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_commandlist.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_compute.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_constants.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_device.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_graphics.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_meshlets.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_queries.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_queue.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_raytracing.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_resource_bindings.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_shader.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_staging_texture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_state_tracking.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_texture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_upload.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_stubs.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_backend_context.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_shader_compiler.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/basic.h"
    "${CMAKE_CURRENT_LIST_DIR}/common.h"
    "${CMAKE_CURRENT_LIST_DIR}/graphics_backend.h"
    "${CMAKE_CURRENT_LIST_DIR}/graphics.h"
    "${CMAKE_CURRENT_LIST_DIR}/render_pass.h"
    "${CMAKE_CURRENT_LIST_DIR}/shader_archive.h"
    "${CMAKE_CURRENT_LIST_DIR}/shader_param.h"
    "${CMAKE_CURRENT_LIST_DIR}/shader_cook.h"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan.h"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_backend.h"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_backend_context.h"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_shader_compiler.h"
)
target_link_libraries(nwb_graphics PUBLIC nwb_logclient nwb_assets nwb_common nwb::volk)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(nwb_graphics PRIVATE nwb::x11)

    if(TARGET nwb::wayland)
        target_link_libraries(nwb_graphics PRIVATE nwb::wayland)
        target_compile_definitions(nwb_graphics PRIVATE NWB_WITH_WAYLAND=1)
    endif()
endif()

if(NWB_BUILD_RESOURCE_COOKER AND TARGET nwb::shaderc_combined)
    nwb_declare_static_library(nwb_graphics_cook)
    target_sources(nwb_graphics_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/shader_cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_shader_compiler.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/shader_cook.h"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/vulkan_shader_compiler.h"
    )
    target_link_libraries(nwb_graphics_cook PUBLIC nwb_logclient nwb_assets nwb_common nwb_metascript nwb::volk nwb::shaderc_combined)
    target_compile_definitions(nwb_graphics_cook PRIVATE NWB_COOK=1)
endif()
