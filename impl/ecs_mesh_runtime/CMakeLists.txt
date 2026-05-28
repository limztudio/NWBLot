nwb_declare_interface_library(nwb_ecs_mesh_runtime)
target_sources(nwb_ecs_mesh_runtime INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/mesh.h"
    "${CMAKE_CURRENT_LIST_DIR}/buffer_upload.h"
)
target_link_libraries(nwb_ecs_mesh_runtime INTERFACE
    nwb_ecs
    nwb_assets
    nwb_graphics
)

