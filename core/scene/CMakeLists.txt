nwb_declare_static_library(nwb_scene)
target_sources(nwb_scene PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/camera.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lighting.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/view.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/camera.h"
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/lighting.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/view.h"
)
target_link_libraries(nwb_scene PUBLIC nwb_ecs)

