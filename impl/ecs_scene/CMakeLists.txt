nwb_declare_static_library(nwb_ecs_scene)
target_sources(nwb_ecs_scene PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/system.h"
)
target_link_libraries(nwb_ecs_scene PUBLIC nwb_ecs)

