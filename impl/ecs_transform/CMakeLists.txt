nwb_declare_static_library(nwb_ecs_transform)
target_sources(nwb_ecs_transform PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_transform.h"
)
target_link_libraries(nwb_ecs_transform PUBLIC nwb_ecs)
