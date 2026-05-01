nwb_declare_static_library(nwb_ecs_deformable)
target_sources(nwb_ecs_deformable PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/deformable_runtime_mesh_cache.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_displacement_runtime.h"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_runtime_helpers.h"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_runtime_mesh_cache.h"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_runtime_names.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_deformable.h"
)
target_link_libraries(nwb_ecs_deformable PUBLIC
    nwb_ecs
    nwb_graphics
    nwb_assets_geometry
    nwb_assets_material
)
target_link_libraries(nwb_ecs_deformable PRIVATE nwb_geometry)
