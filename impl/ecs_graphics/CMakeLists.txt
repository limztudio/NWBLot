nwb_declare_static_library(ecs_graphics)
target_sources(ecs_graphics PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/renderer_system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_graphics.h"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_system.h"
)
target_link_libraries(ecs_graphics PUBLIC ecs graphics)
