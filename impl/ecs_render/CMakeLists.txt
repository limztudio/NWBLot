nwb_declare_static_library(nwb_ecs_render)
target_sources(nwb_ecs_render PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/renderer_system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_render.h"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_system.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_geometry.h"
    "${CMAKE_CURRENT_LIST_DIR}/shader_asset_loader.h"
)
target_link_libraries(nwb_ecs_render PUBLIC
    nwb_ecs
    nwb_scene
    nwb_graphics
    nwb_assets_geometry
    nwb_assets_material
    nwb_assets_shader
)
target_link_libraries(nwb_ecs_render PRIVATE nwb_geometry)
