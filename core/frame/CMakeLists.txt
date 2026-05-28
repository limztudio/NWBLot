nwb_declare_static_library(nwb_frame)
target_sources(nwb_frame PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/input_helpers.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
)

if(WIN32)
    target_sources(nwb_frame PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/win32.cpp"
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(nwb_frame PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/linux.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/linux_platform.h"
        "${CMAKE_CURRENT_LIST_DIR}/linux_x11.cpp"
    )
    target_link_libraries(nwb_frame PRIVATE nwb::x11)

    if(TARGET nwb::wayland)
        target_sources(nwb_frame PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/linux_wayland.cpp"
        )
        target_link_libraries(nwb_frame PRIVATE nwb::wayland)
        target_compile_definitions(nwb_frame PRIVATE NWB_WITH_WAYLAND=1)
        nwb_enable_wayland_protocol(nwb_frame)
    endif()
endif()

target_link_libraries(nwb_frame PUBLIC
    nwb_common
    nwb_alloc
    nwb_graphics
    nwb_input
)
