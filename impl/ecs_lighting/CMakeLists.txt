nwb_declare_static_library(nwb_ecs_lighting)
target_sources(nwb_ecs_lighting PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/system.h"
)
target_link_libraries(nwb_ecs_lighting PUBLIC
    nwb_ecs
    nwb_ecs_scene
)

