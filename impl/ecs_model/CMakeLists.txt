nwb_declare_static_library(nwb_ecs_model)
target_sources(nwb_ecs_model PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/system_transforms.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/system.h"
    "${CMAKE_CURRENT_LIST_DIR}/transform_helpers.h"
)
nwb_target_adhd_sources(nwb_ecs_model
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/components.adhd"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/impl/assets_skeleton/joint_types.adhd"
)
target_link_libraries(nwb_ecs_model PUBLIC nwb_ecs nwb_assets_skeleton_types)
target_link_libraries(nwb_ecs_model PRIVATE nwb_assets_model nwb_ecs_mesh nwb_ecs_scene nwb_ecs_skeleton)
