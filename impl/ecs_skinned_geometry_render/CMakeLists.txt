nwb_declare_static_library(nwb_ecs_skinned_geometry_render)
target_sources(nwb_ecs_skinned_geometry_render PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_runtime_mesh_cache.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_runtime_mesh_cache_resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_runtime_mesh_cache_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_system_skinning.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_runtime_mesh_cache.h"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_runtime_mesh.h"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_runtime_resource_names.h"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_skin_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_system.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_skinned_geometry_render.h"
)
target_link_libraries(nwb_ecs_skinned_geometry_render PUBLIC
    nwb_ecs_skinned_geometry
    nwb_ecs_geometry
    nwb_graphics
)
target_link_libraries(nwb_ecs_skinned_geometry_render PRIVATE
    nwb_assets_geometry
    nwb_assets_shader_loader
    nwb_ecs_render_components
)

