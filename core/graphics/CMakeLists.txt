set(NWB_GRAPHICS_BACKEND "Vulkan" CACHE STRING "Graphics backend provider.")
set_property(CACHE NWB_GRAPHICS_BACKEND PROPERTY STRINGS Vulkan Metal)
if(NWB_GRAPHICS_BACKEND STREQUAL "Metal")
    message(FATAL_ERROR "The Metal graphics backend is not implemented yet.")
elseif(NOT NWB_GRAPHICS_BACKEND STREQUAL "Vulkan")
    message(FATAL_ERROR "Unknown graphics backend: ${NWB_GRAPHICS_BACKEND}")
endif()

nwb_declare_static_library(nwb_shader_bytecode)
nwb_target_adhd_sources(nwb_shader_bytecode NAMESPACE "NWB::Core" SOURCES "${CMAKE_CURRENT_LIST_DIR}/spirv_model.adhd")
target_sources(nwb_shader_bytecode PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/spirv_entry_point.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/spirv_entry_point.h"
)
target_link_libraries(nwb_shader_bytecode PUBLIC nwb_common nwb_graphics_backend_ray_policy)

nwb_declare_static_library(nwb_shader_archive)
target_sources(nwb_shader_archive PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/shader_archive.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/shader_archive.h"
)
target_link_libraries(nwb_shader_archive PUBLIC nwb_common nwb_graphics_backend_ray_policy)

nwb_declare_static_library(nwb_graphics_internal_names)
nwb_target_adhd_sources(nwb_graphics_internal_names NAMESPACE "NWB::Core::GraphicsInternalNames" SOURCES "${CMAKE_CURRENT_LIST_DIR}/internal_names.adhd")

nwb_declare_static_library(nwb_graphics_backend_contract_types)
nwb_target_adhd_sources(nwb_graphics_backend_contract_types NAMESPACE "NWB::Core::GraphicsBackend" SOURCES "${CMAKE_CURRENT_LIST_DIR}/backend_selection/raster_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/backend_selection/texture_contract_requirements.adhd")
target_link_libraries(nwb_graphics_backend_contract_types PUBLIC nwb_common)
target_link_libraries(nwb_graphics_backend_contract_types PUBLIC nwb_graphics_backend_queue_sharing nwb_graphics_backend_ray_policy)

if(NWB_GRAPHICS_BACKEND STREQUAL "Vulkan")
    nwb_declare_static_library(nwb_graphics_backend_vulkan_defaults)
    nwb_target_adhd_sources(nwb_graphics_backend_vulkan_defaults NAMESPACE "NWB::Core::GraphicsBackend::VulkanDefaults" SOURCES "${CMAKE_CURRENT_LIST_DIR}/vulkan/default_values.adhd")

    nwb_declare_static_library(nwb_graphics_backend_vulkan_arena_names)
    nwb_target_adhd_sources(nwb_graphics_backend_vulkan_arena_names NAMESPACE "NWB::Core::GraphicsBackend::VulkanArenaScope" SOURCES "${CMAKE_CURRENT_LIST_DIR}/vulkan/arena_names.adhd")

    nwb_declare_static_library(nwb_graphics_backend_vulkan_types)
    nwb_target_adhd_sources(nwb_graphics_backend_vulkan_types NAMESPACE "NWB::Core::GraphicsBackend::VulkanDetail" SOURCES "${CMAKE_CURRENT_LIST_DIR}/vulkan/heap_binding_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/vulkan/timer_query_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/vulkan/context_labels.adhd" "${CMAKE_CURRENT_LIST_DIR}/vulkan/backend_context_capability_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/vulkan/host_readback_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/vulkan/texture_transfer_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/vulkan/raytracing_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/vulkan/device_feature_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/vulkan/pipeline_cache_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/vulkan/swap_chain_presentation_types.adhd")
    target_link_libraries(nwb_graphics_backend_vulkan_types PUBLIC nwb_graphics_backend_vulkan_defaults nwb_common nwb::volk)
endif()

nwb_declare_static_library(nwb_graphics_backend)
nwb_target_adhd_sources(nwb_graphics_backend NAMESPACE "NWB::Core" SOURCES "${CMAKE_CURRENT_LIST_DIR}/rhi/ownership.adhd" "${CMAKE_CURRENT_LIST_DIR}/gpu_timing_metrics.adhd" "${CMAKE_CURRENT_LIST_DIR}/rhi/blend_validation.adhd" "${CMAKE_CURRENT_LIST_DIR}/rhi/coopvec_size.adhd" "${CMAKE_CURRENT_LIST_DIR}/rhi/framebuffer_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/vulkan/texture_clear_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/vulkan/coopvec_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/vulkan/input_layout_types.adhd")
nwb_declare_static_library(nwb_graphics_backend_ray_policy)
nwb_target_adhd_sources(nwb_graphics_backend_ray_policy NAMESPACE "NWB::Core::HardwareRayTracingPolicy" SOURCES "${CMAKE_CURRENT_LIST_DIR}/rhi/hardware_ray_tracing_policy.adhd")
nwb_declare_static_library(nwb_graphics_backend_queue_sharing)
nwb_target_adhd_sources(nwb_graphics_backend_queue_sharing NAMESPACE "NWB::Core::ResourceQueueSharing" SOURCES "${CMAKE_CURRENT_LIST_DIR}/rhi/queue_sharing.adhd")
target_link_libraries(nwb_graphics_backend_queue_sharing PUBLIC nwb_graphics_backend_ray_policy)
target_sources(nwb_graphics_backend PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/gpu_crash.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_timing*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_overlap_metric_tracker.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/backend_selection/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/backend_selection/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/backend_selection/*.cxx"
    "${CMAKE_CURRENT_LIST_DIR}/backend_selection/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/backend_selection/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/backend_selection/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/backend_selection/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/backend_selection/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/backend_selection/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/backend_selection/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/backend_selection/*.ixx"
    "${CMAKE_CURRENT_LIST_DIR}/rhi/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/rhi/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/rhi/*.cxx"
    "${CMAKE_CURRENT_LIST_DIR}/rhi/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/rhi/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/rhi/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/rhi/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/rhi/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/rhi/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/rhi/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/rhi/*.ixx"
)
if(NWB_GRAPHICS_BACKEND STREQUAL "Vulkan")
    target_compile_definitions(nwb_graphics_backend PUBLIC NWB_GRAPHICS_BACKEND_VULKAN=1 NWB_GRAPHICS_BACKEND_METAL=0)
    target_sources(nwb_graphics_backend PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/*.c"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/*.cxx"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/*.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/*.h"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/*.hh"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/*.hxx"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/*.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/*.inl"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/*.ipp"
        "${CMAKE_CURRENT_LIST_DIR}/vulkan/*.ixx"
    )
endif()
target_link_libraries(nwb_graphics_backend PUBLIC nwb_common nwb_cpu_task nwb_shader_bytecode nwb_graphics_backend_contract_types)
target_link_libraries(nwb_graphics_backend PUBLIC nwb_graphics_backend_queue_sharing nwb_graphics_backend_ray_policy)
target_link_libraries(nwb_graphics_backend PUBLIC nwb_perf)
target_link_libraries(nwb_graphics_backend PUBLIC nwb_telemetry)
target_link_libraries(nwb_graphics_backend PRIVATE nwb_filesystem nwb_graphics_internal_names)
if(NWB_GRAPHICS_BACKEND STREQUAL "Vulkan")
    target_link_libraries(nwb_graphics_backend PUBLIC nwb::volk nwb_graphics_backend_vulkan_types nwb_graphics_backend_vulkan_arena_names)
    target_link_libraries(nwb_graphics_backend PRIVATE nwb::vma_headers nwb::aftermath_headers)
endif()

if(NWB_GRAPHICS_BACKEND STREQUAL "Vulkan" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(nwb_graphics_backend PRIVATE nwb::x11)

    if(TARGET nwb::wayland)
        target_link_libraries(nwb_graphics_backend PRIVATE nwb::wayland)
        target_compile_definitions(nwb_graphics_backend PRIVATE NWB_WITH_WAYLAND=1)
    endif()
endif()

add_subdirectory(runtime)

