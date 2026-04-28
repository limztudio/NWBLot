nwb_declare_static_library(nwb_ecs_ui)
target_sources(nwb_ecs_ui PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_ui.h"
    "${CMAKE_CURRENT_LIST_DIR}/ui_system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui_system.h"
)
target_link_libraries(nwb_ecs_ui PUBLIC
    nwb_assets_graphics
    nwb_ecs
    nwb_graphics
    nwb_input
    nwb::imgui
)
target_link_libraries(nwb_ecs_ui PRIVATE
    nwb_logclient
)
