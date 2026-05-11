nwb_declare_interface_library(nwb_ecs_deformable)
target_sources(nwb_ecs_deformable INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_displacement_runtime.h"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_runtime_helpers.h"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_runtime_mesh.h"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_runtime_names.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_deformable.h"
)
target_link_libraries(nwb_ecs_deformable INTERFACE
    nwb_ecs
    nwb_assets_geometry
    nwb_graphics
)
