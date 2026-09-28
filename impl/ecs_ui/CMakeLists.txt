nwb_declare_static_library(nwb_ecs_ui)
target_sources(nwb_ecs_ui PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/system_task_tasks.h"
    "${CMAKE_CURRENT_LIST_DIR}/system_legacy_presentation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/system_frame.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/input_events.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/graphics_resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/texture_resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/texture_descriptor_heap.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/texture_submission.h"
    "${CMAKE_CURRENT_LIST_DIR}/ui_internal.h"
    "${CMAKE_CURRENT_LIST_DIR}/ui_presentation_helpers.h"
    "${CMAKE_CURRENT_LIST_DIR}/system.h"
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
    nwb_ui_gpu
    nwb_ui_text
    nwb::imgui
)
target_link_libraries(nwb_ecs_ui PRIVATE nwb_assets_shader_loader)
