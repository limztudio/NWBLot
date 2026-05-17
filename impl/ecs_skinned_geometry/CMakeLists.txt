nwb_declare_interface_library(nwb_ecs_skinned_geometry)
target_sources(nwb_ecs_skinned_geometry INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_runtime_helpers.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_skinned_geometry.h"
)
target_link_libraries(nwb_ecs_skinned_geometry INTERFACE
    nwb_ecs
    nwb_assets
    nwb_skinned_geometry_types
)
