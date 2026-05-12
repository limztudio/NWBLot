nwb_declare_static_library(nwb_ecs_lighting)
target_sources(nwb_ecs_lighting PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/lighting.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_lighting.h"
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/lighting.h"
)
target_link_libraries(nwb_ecs_lighting PUBLIC nwb_ecs_scene)

