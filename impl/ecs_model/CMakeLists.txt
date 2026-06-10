nwb_declare_interface_library(nwb_ecs_model)
target_sources(nwb_ecs_model INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
)
target_link_libraries(nwb_ecs_model INTERFACE
    nwb_ecs
    nwb_assets_model
)
