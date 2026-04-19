nwb_declare_static_library(nwb_scene)
target_sources(nwb_scene PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/transform_component.h"
    "${CMAKE_CURRENT_LIST_DIR}/camera_component.h"
    "${CMAKE_CURRENT_LIST_DIR}/light_component.h"
    "${CMAKE_CURRENT_LIST_DIR}/scene_component.h"
    "${CMAKE_CURRENT_LIST_DIR}/scene.h"
)
target_link_libraries(nwb_scene PUBLIC nwb_ecs)
