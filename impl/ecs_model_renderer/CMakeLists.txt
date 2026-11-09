nwb_declare_static_library(nwb_ecs_model_renderer)
nwb_target_adhd_sources(nwb_ecs_model_renderer
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
)
target_link_libraries(nwb_ecs_model_renderer PUBLIC nwb_ecs_model nwb_ecs_render_components)
