nwb_declare_static_library(nwb_frame)
target_sources(nwb_frame PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/frame.h"
    "${CMAKE_CURRENT_LIST_DIR}/frame.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame_win.cpp"
)
target_link_libraries(nwb_frame PUBLIC nwb_logclient nwb_common nwb_ecs nwb_ecs_graphics nwb_graphics nwb_assets_graphics)
