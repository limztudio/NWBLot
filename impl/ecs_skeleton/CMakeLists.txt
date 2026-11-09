nwb_declare_static_library(nwb_ecs_skeleton)
nwb_target_adhd_sources(nwb_ecs_skeleton
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
)
target_link_libraries(nwb_ecs_skeleton PUBLIC nwb_ecs nwb_assets nwb_assets_skeleton_types)
