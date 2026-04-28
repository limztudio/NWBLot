nwb_declare_static_library(nwb_loader)
target_sources(nwb_loader PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/project_entry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/pch.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/pch.h"
    "${CMAKE_CURRENT_LIST_DIR}/project_entry.h"
)
target_link_libraries(nwb_loader PRIVATE
    nwb_assets
    nwb_common
    nwb_alloc
    nwb_ecs_graphics
    nwb_ecs_ui
    nwb_filesystem
    nwb_frame
    nwb_graphics
    nwb_logclient
    nwb::cli11_headers
)
