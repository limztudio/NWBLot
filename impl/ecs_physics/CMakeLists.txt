nwb_declare_static_library(nwb_ecs_physics_components)
nwb_target_adhd_sources(nwb_ecs_physics_components
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/components.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/subsystem_components.adhd"
)
target_link_libraries(nwb_ecs_physics_components PUBLIC nwb_ecs nwb_ecs_scene nwb_physics nwb_assets nwb_assets_mesh_types nwb_assets_physics_data_types)

nwb_declare_static_library(nwb_ecs_physics)
nwb_target_adhd_sources(nwb_ecs_physics
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/debug_draw.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/system.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/system_sync.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/subsystem_bridges.adhd"
)
target_link_libraries(nwb_ecs_physics PUBLIC nwb_ecs_physics_components nwb_physics_gpu nwb_assets_physics_types nwb_assets_scene nwb_assets_mesh nwb_assets_shader_loader nwb_graphics nwb_cpu_task nwb_gpu_task nwb_ecs_skeleton nwb_ecs_mesh_components nwb_ecs_csg)
