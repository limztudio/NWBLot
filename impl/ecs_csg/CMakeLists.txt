nwb_declare_static_library(nwb_ecs_csg)
nwb_target_adhd_sources(nwb_ecs_csg
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
)
target_link_libraries(nwb_ecs_csg PUBLIC nwb_ecs nwb_assets_csg)
