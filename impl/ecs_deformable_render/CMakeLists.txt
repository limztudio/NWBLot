nwb_declare_static_library(nwb_ecs_deformable_render)
target_sources(nwb_ecs_deformable_render PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/deformer_system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/deformer_morph_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/deformer_skin_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/deformer_system.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_deformable_render.h"
)
target_link_libraries(nwb_ecs_deformable_render PUBLIC
    nwb_ecs_deformable
    nwb_ecs_render
    nwb_assets_shader
    nwb_graphics
)
target_link_libraries(nwb_ecs_deformable_render PRIVATE nwb_geometry)
