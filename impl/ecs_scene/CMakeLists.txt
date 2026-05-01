nwb_declare_static_library(nwb_ecs_scene)
target_sources(nwb_ecs_scene PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_scene.h"
    "${CMAKE_CURRENT_LIST_DIR}/scene.h"
)
target_link_libraries(nwb_ecs_scene PUBLIC nwb_ecs nwb_ecs_transform nwb_ecs_camera)
