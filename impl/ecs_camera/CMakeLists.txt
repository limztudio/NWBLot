nwb_declare_interface_library(nwb_ecs_camera)
target_sources(nwb_ecs_camera INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/ecs_camera.h"
    "${CMAKE_CURRENT_LIST_DIR}/camera_component.h"
)
target_link_libraries(nwb_ecs_camera INTERFACE nwb_common)

