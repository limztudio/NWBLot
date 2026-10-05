nwb_declare_static_library(nwb_ecs_model)
target_sources(nwb_ecs_model PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/system.h"
)
target_link_libraries(nwb_ecs_model PUBLIC
    nwb_ecs
    nwb_assets_skeleton_types
)
target_link_libraries(nwb_ecs_model PRIVATE
    nwb_assets_model
    nwb_ecs_mesh
    nwb_ecs_scene
    nwb_ecs_skeleton
)
