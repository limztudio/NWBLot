nwb_declare_static_library(nwb_ecs_camera)
target_sources(nwb_ecs_camera PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_camera.h"
)
target_link_libraries(nwb_ecs_camera PUBLIC nwb_ecs)
