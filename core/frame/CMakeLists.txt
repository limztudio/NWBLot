nwb_declare_static_library(nwb_frame)
target_sources(nwb_frame PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/frame.h"
    "${CMAKE_CURRENT_LIST_DIR}/frame_input_helpers.h"
    "${CMAKE_CURRENT_LIST_DIR}/frame.cpp"
)

if(WIN32)
    target_sources(nwb_frame PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/frame_win.cpp"
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(nwb_frame PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/frame_linux.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/frame_linux_platform.h"
        "${CMAKE_CURRENT_LIST_DIR}/frame_linux_x11.cpp"
    )
    target_link_libraries(nwb_frame PRIVATE nwb::x11)

    if(TARGET nwb::wayland)
        target_sources(nwb_frame PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/frame_linux_wayland.cpp"
        )
        target_link_libraries(nwb_frame PRIVATE nwb::wayland)
        target_compile_definitions(nwb_frame PRIVATE NWB_WITH_WAYLAND=1)
        nwb_enable_wayland_protocol(nwb_frame)
    endif()
endif()

target_link_libraries(nwb_frame PUBLIC
    nwb_alloc
    nwb_common
    nwb_graphics
)
target_link_libraries(nwb_frame PRIVATE nwb_logclient)
