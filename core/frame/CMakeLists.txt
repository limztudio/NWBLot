nwb_declare_static_library(nwb_frame_arena_names)
nwb_target_adhd_sources(nwb_frame_arena_names NAMESPACE "NWB::Core::FrameArenaScope" SOURCES "${CMAKE_CURRENT_LIST_DIR}/arena_names.adhd")

nwb_declare_static_library(nwb_frame)
nwb_target_adhd_sources(nwb_frame NAMESPACE "NWB::Core::FrameDetail" SOURCES "${CMAKE_CURRENT_LIST_DIR}/frame_dimensions.adhd" "${CMAKE_CURRENT_LIST_DIR}/frame_timing_names.adhd" "${CMAKE_CURRENT_LIST_DIR}/x11_scroll_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/frame_platform_values.adhd")
target_sources(nwb_frame PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
)

if(WIN32)
    target_sources(nwb_frame PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/win32*.cpp"
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(nwb_frame PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/linux.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/linux_x11.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/linux_x11_keyboard.cpp"
    )
    target_link_libraries(nwb_frame PRIVATE nwb::x11)

    if(TARGET nwb::wayland)
        target_sources(nwb_frame PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/linux_wayland*.cpp"
        )
        target_link_libraries(nwb_frame PRIVATE nwb::wayland)
        target_compile_definitions(nwb_frame PRIVATE NWB_WITH_WAYLAND=1)
        nwb_enable_wayland_protocol(nwb_frame)
    endif()
endif()

target_link_libraries(nwb_frame PRIVATE nwb_frame_arena_names)
target_link_libraries(nwb_frame PUBLIC nwb_common nwb_alloc nwb_cpu_task nwb_graphics nwb_input nwb_os nwb_perf nwb_telemetry)

