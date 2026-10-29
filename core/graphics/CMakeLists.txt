set(NWB_GRAPHICS_BACKEND "Vulkan" CACHE STRING "Graphics backend provider.")
set_property(CACHE NWB_GRAPHICS_BACKEND PROPERTY STRINGS Vulkan Metal)
if(NWB_GRAPHICS_BACKEND STREQUAL "Metal")
    message(FATAL_ERROR "The Metal graphics backend is not implemented yet.")
elseif(NOT NWB_GRAPHICS_BACKEND STREQUAL "Vulkan")
    message(FATAL_ERROR "Unknown graphics backend: ${NWB_GRAPHICS_BACKEND}")
endif()

nwb_declare_static_library(nwb_shader_bytecode)
target_sources(nwb_shader_bytecode PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/spirv_entry_point.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/spirv_entry_point.h"
)
target_link_libraries(nwb_shader_bytecode PUBLIC nwb_common)

nwb_declare_static_library(nwb_shader_archive)
target_sources(nwb_shader_archive PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/shader_archive.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/shader_archive.h"
)
target_link_libraries(nwb_shader_archive PUBLIC nwb_common)

nwb_declare_static_library(nwb_graphics_backend)
target_sources(nwb_graphics_backend PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/backend_contract.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_crash.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_timing*.cpp"
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
target_link_libraries(nwb_graphics_backend PUBLIC nwb_common nwb_cpu_task nwb_shader_bytecode)
target_link_libraries(nwb_graphics_backend PUBLIC nwb_perf)
target_link_libraries(nwb_graphics_backend PUBLIC nwb_telemetry)
target_link_libraries(nwb_graphics_backend PRIVATE nwb_filesystem)
if(NWB_GRAPHICS_BACKEND STREQUAL "Vulkan")
    target_link_libraries(nwb_graphics_backend PUBLIC nwb::volk)
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
