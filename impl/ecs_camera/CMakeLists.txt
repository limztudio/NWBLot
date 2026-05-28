nwb_declare_static_library(nwb_ecs_camera)
target_sources(nwb_ecs_camera PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/system.h"
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
)
target_link_libraries(nwb_ecs_camera PUBLIC
    nwb_ecs
    nwb_ecs_scene
)

