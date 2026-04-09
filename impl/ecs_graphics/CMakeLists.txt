nwb_declare_static_library(nwb_ecs_graphics)
target_sources(nwb_ecs_graphics PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/renderer_system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_graphics.h"
    "${CMAKE_CURRENT_LIST_DIR}/renderer_system.h"
)
target_link_libraries(nwb_ecs_graphics PUBLIC nwb_ecs nwb_graphics)
target_link_libraries(nwb_ecs_graphics PRIVATE nwb_assets_graphics)
