nwb_declare_interface_library(nwb_ecs_render_components)
target_sources(nwb_ecs_render_components INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/material_instance.h"
)
target_link_libraries(nwb_ecs_render_components INTERFACE
    nwb_assets
    nwb_assets_material
    nwb_ecs
)

nwb_declare_static_library(nwb_ecs_render)
target_sources(nwb_ecs_render PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/avboit.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/avboit_resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/avboit_targets.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/deferred.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/draw.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/draw_resources.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/mesh.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lighting.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/material.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/material_instance.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/shader.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/avboit.h"
    "${CMAKE_CURRENT_LIST_DIR}/system.h"
    "${CMAKE_CURRENT_LIST_DIR}/avboit_private.h"
    "${CMAKE_CURRENT_LIST_DIR}/mesh_view_private.h"
    "${CMAKE_CURRENT_LIST_DIR}/private.h"
    "${CMAKE_CURRENT_LIST_DIR}/timing_names.h"
)
target_link_libraries(nwb_ecs_render PUBLIC
    nwb_ecs_render_components
    nwb_ecs
    nwb_ecs_mesh
    nwb_ecs_mesh_runtime
    nwb_graphics
    nwb_assets_material
)
target_link_libraries(nwb_ecs_render PRIVATE
    nwb_assets_mesh
    nwb_assets_shader_loader
    nwb_ecs_scene
)

