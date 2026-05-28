nwb_declare_static_library(nwb_graphics)
target_sources(nwb_graphics PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/basic.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/common.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/shader_archive.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/spirv_entry_point.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/allocator.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/buffer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/commandlist.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/compute.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/constants.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/device.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/extensions.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/graphics_pipeline.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/meshlets.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/queries.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/queue.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/raytracing.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/resource_bindings.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/shader.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/staging_texture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/state_tracking.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/sampler_feedback.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/texture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/upload.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/backend_context.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/basic.h"
    "${CMAKE_CURRENT_LIST_DIR}/common.h"
    "${CMAKE_CURRENT_LIST_DIR}/backend.h"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_timing.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/render_pass.h"
    "${CMAKE_CURRENT_LIST_DIR}/shader_archive.h"
    "${CMAKE_CURRENT_LIST_DIR}/shader_param.h"
    "${CMAKE_CURRENT_LIST_DIR}/shader_stage_names.h"
    "${CMAKE_CURRENT_LIST_DIR}/spirv_entry_point.h"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/backend.h"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/backend_context.h"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan/vma.h"
)
target_link_libraries(nwb_graphics PUBLIC nwb_common)
target_link_libraries(nwb_graphics PRIVATE nwb::volk)
target_link_libraries(nwb_graphics PRIVATE nwb::vma_headers)
target_link_libraries(nwb_graphics PRIVATE nwb_filesystem)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(nwb_graphics PRIVATE nwb::x11)

    if(TARGET nwb::wayland)
        target_link_libraries(nwb_graphics PRIVATE nwb::wayland)
        target_compile_definitions(nwb_graphics PRIVATE NWB_WITH_WAYLAND=1)
    endif()
endif()


