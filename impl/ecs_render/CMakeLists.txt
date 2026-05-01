nwb_declare_static_library(nwb_ecs_render)
target_sources(nwb_ecs_render PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/renderer_system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_render.h"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_system.h"
    "${CMAKE_CURRENT_LIST_DIR}/shader_asset_loader.h"
)
target_link_libraries(nwb_ecs_render PUBLIC
    nwb_ecs
    nwb_ecs_transform
    nwb_ecs_camera
    nwb_ecs_scene
    nwb_ecs_lighting
    nwb_ecs_deformable
    nwb_graphics
    nwb_assets_geometry
    nwb_assets_material
    nwb_assets_shader
)
target_link_libraries(nwb_ecs_render PRIVATE nwb_geometry)
