nwb_declare_static_library(nwb_ecs_deformable_edit)
target_sources(nwb_ecs_deformable_edit PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/deformable_debug_draw.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_operator_shape.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_picking.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_surface_edit.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_debug_draw.h"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_operator_shape.h"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_picking.h"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_surface_edit.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_deformable_edit.h"
)
target_link_libraries(nwb_ecs_deformable_edit PUBLIC
    nwb_ecs_deformable
    nwb_scene
    nwb_assets_geometry
    nwb_assets_material
)
target_link_libraries(nwb_ecs_deformable_edit PRIVATE
    nwb_ecs_render
    nwb_geometry
)
