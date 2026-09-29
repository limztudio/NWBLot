nwb_declare_static_library(nwb_ecs_ui)
target_sources(nwb_ecs_ui PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/text_edit_session.h"
    "${CMAKE_CURRENT_LIST_DIR}/text_edit_session.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/layer_input.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/layer_system.h"
    "${CMAKE_CURRENT_LIST_DIR}/layer_system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/layer_system_frame.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/layer_system_fonts.cpp"
)
target_link_libraries(nwb_ecs_ui PUBLIC
    nwb_assets
    nwb_ecs
    nwb_graphics
    nwb_input
    nwb_os
    nwb_ui_edit
    nwb_ui_gpu
    nwb_ui_text
    nwb_ui_widgets
)
