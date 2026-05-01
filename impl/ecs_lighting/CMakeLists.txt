add_library(nwb_ecs_lighting INTERFACE)
target_sources(nwb_ecs_lighting INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_lighting.h"
)
target_link_libraries(nwb_ecs_lighting INTERFACE nwb_common)
