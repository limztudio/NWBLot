nwb_declare_static_library(nwb_ecs_model_components)
nwb_target_adhd_sources(nwb_ecs_model_components
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/components.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/types.adhd"
)
target_link_libraries(nwb_ecs_model_components PUBLIC nwb_ecs nwb_assets nwb_assets_skeleton_types)

nwb_declare_static_library(nwb_ecs_model)
nwb_target_adhd_sources(nwb_ecs_model
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/system.adhd"
)
target_link_libraries(nwb_ecs_model PUBLIC nwb_ecs_model_components nwb_ecs nwb_assets_skeleton_types)
target_link_libraries(nwb_ecs_model PRIVATE nwb_assets_model nwb_ecs_mesh nwb_ecs_scene nwb_ecs_skeleton)
