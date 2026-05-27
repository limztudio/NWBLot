nwb_declare_static_library(nwb_ecs_skinned_mesh_render)
target_sources(nwb_ecs_skinned_mesh_render PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_runtime_mesh_cache.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_runtime_mesh_cache_resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_runtime_mesh_cache_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_system_skinning.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_runtime_mesh_cache.h"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_runtime_mesh.h"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_runtime_resource_names.h"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_skin_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_system.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_skinned_mesh_render.h"
)
target_link_libraries(nwb_ecs_skinned_mesh_render PUBLIC
    nwb_ecs_skinned_mesh
    nwb_ecs_mesh
    nwb_graphics
)
target_link_libraries(nwb_ecs_skinned_mesh_render PRIVATE
    nwb_assets_mesh
    nwb_assets_shader_loader
    nwb_ecs_render_components
)

