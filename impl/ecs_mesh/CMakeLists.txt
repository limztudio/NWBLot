nwb_declare_static_library(nwb_ecs_mesh)
target_sources(nwb_ecs_mesh PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/system.h"
)
target_link_libraries(nwb_ecs_mesh PUBLIC
    nwb_ecs
    nwb_assets
    nwb_ecs_mesh_runtime
)

