nwb_declare_static_library(nwb_ecs_mesh_soft_body)
nwb_target_adhd_sources(nwb_ecs_mesh_soft_body
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/factory.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/cpu_mesh.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/system.adhd"
)
target_link_libraries(nwb_ecs_mesh_soft_body PUBLIC nwb_ecs_mesh nwb_ecs_physics_components nwb_physics_gpu nwb_ecs_scene nwb_graphics nwb_assets_mesh nwb_gpu_task)
