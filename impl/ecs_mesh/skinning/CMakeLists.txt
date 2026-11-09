nwb_declare_static_library(nwb_ecs_mesh_skinning)
nwb_target_adhd_sources(nwb_ecs_mesh_skinning
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
)
target_link_libraries(nwb_ecs_mesh_skinning PUBLIC nwb_ecs_mesh nwb_ecs_skeleton nwb_graphics nwb_assets_mesh nwb_assets_scene)
target_link_libraries(nwb_ecs_mesh_skinning PRIVATE nwb_assets_shader_loader)
