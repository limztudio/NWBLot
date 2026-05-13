nwb_declare_static_library(nwb_scene)
target_sources(nwb_scene PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/lighting.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/lighting.h"
    "${CMAKE_CURRENT_LIST_DIR}/scene.h"
)
target_link_libraries(nwb_scene PUBLIC nwb_ecs)

