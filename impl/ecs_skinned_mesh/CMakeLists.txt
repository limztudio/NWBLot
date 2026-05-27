nwb_declare_interface_library(nwb_ecs_skinned_mesh)
target_sources(nwb_ecs_skinned_mesh INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_runtime_helpers.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_skinned_mesh.h"
)
target_link_libraries(nwb_ecs_skinned_mesh INTERFACE
    nwb_ecs
    nwb_assets
    nwb_skinned_mesh_types
)
