nwb_declare_interface_library(nwb_ecs_lighting)
target_sources(nwb_ecs_lighting INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/ecs_lighting.h"
    "${CMAKE_CURRENT_LIST_DIR}/light_component.h"
)
target_link_libraries(nwb_ecs_lighting INTERFACE nwb_common)

