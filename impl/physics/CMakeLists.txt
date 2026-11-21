nwb_declare_static_library(nwb_physics)
nwb_target_adhd_sources(nwb_physics
    NAMESPACE "NWB::Impl::Physics"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
)
target_link_libraries(nwb_physics PUBLIC nwb_common nwb_alloc nwb_cpu_task)
set_target_properties(nwb_physics PROPERTIES UNITY_BUILD_BATCH_SIZE 32)

nwb_declare_static_library(nwb_physics_gpu)
nwb_target_adhd_sources(nwb_physics_gpu
    NAMESPACE "NWB::Impl::Physics"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/gpu/*.adhd"
)
target_link_libraries(nwb_physics_gpu PUBLIC nwb_physics nwb_gpu_task nwb_graphics nwb_assets_physics_types nwb_assets_scene nwb_assets_shader_loader)
