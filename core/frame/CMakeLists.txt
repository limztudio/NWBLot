nwb_declare_static_library(frame)
target_sources(frame PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/frame.h"
    "${CMAKE_CURRENT_LIST_DIR}/frame.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame_win.cpp"
)
target_link_libraries(frame PUBLIC logclient common ecs ecs_graphics graphics assets_graphics)
