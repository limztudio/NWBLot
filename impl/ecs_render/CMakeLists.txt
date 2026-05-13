nwb_declare_interface_library(nwb_ecs_render_components)
target_sources(nwb_ecs_render_components INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
)
target_link_libraries(nwb_ecs_render_components INTERFACE nwb_assets)

nwb_declare_static_library(nwb_ecs_render)
target_sources(nwb_ecs_render PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/renderer_system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_system_avboit.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_system_deferred.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_system_draw.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_system_geometry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_system_lighting.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_system_material.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_system_shader.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_render.h"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_avboit.h"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_system.h"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_system_private.h"
)
target_link_libraries(nwb_ecs_render PUBLIC
    nwb_ecs_render_components
    nwb_ecs
    nwb_ecs_geometry
    nwb_graphics
    nwb_assets_material
)
target_link_libraries(nwb_ecs_render PRIVATE nwb_assets_geometry nwb_assets_shader_loader nwb_geometry nwb_scene)
