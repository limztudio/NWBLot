nwb_declare_interface_library(nwb_ecs_transform)
target_sources(nwb_ecs_transform INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_transform.h"
)
target_link_libraries(nwb_ecs_transform INTERFACE nwb_ecs)
