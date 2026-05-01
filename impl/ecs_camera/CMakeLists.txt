nwb_declare_interface_library(nwb_ecs_camera)
target_sources(nwb_ecs_camera INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_camera.h"
)
target_link_libraries(nwb_ecs_camera INTERFACE nwb_ecs)
