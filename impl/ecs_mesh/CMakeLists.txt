nwb_declare_static_library(nwb_ecs_mesh_components)
nwb_target_adhd_sources(nwb_ecs_mesh_components
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/components.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime/buffer_names.adhd"
)
target_link_libraries(nwb_ecs_mesh_components PUBLIC nwb_ecs nwb_assets nwb_assets_skeleton_types nwb_assets_mesh_types)

nwb_declare_static_library(nwb_ecs_mesh)
nwb_target_adhd_sources(nwb_ecs_mesh
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/system.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime/types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime/buffer_upload.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime/mesh.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime/mesh_requests.adhd"
)
target_link_libraries(nwb_ecs_mesh PUBLIC nwb_ecs_mesh_components nwb_ecs nwb_assets nwb_graphics nwb_assets_skeleton_types nwb_assets_mesh_types)

add_subdirectory(skinning)
