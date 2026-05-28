nwb_declare_interface_library(nwb_ecs_skinned_mesh)
target_sources(nwb_ecs_skinned_mesh INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_helpers.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
)
target_link_libraries(nwb_ecs_skinned_mesh INTERFACE
    nwb_ecs
    nwb_assets
    nwb_skinned_mesh_types
)
