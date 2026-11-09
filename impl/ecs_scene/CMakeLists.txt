nwb_declare_static_library(nwb_ecs_scene)
nwb_target_adhd_sources(nwb_ecs_scene
    NAMESPACE "NWB::Impl::Scene"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
)
target_link_libraries(nwb_ecs_scene PUBLIC nwb_ecs)
