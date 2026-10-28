nwb_declare_static_library(nwb_ecs_scene)
target_sources(nwb_ecs_scene PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/camera.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lighting.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/view.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/camera.h"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/lighting.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/view.h"
)
nwb_target_adhd_sources(nwb_ecs_scene
    NAMESPACE "NWB::Impl::Scene"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/components.adhd"
)
target_link_libraries(nwb_ecs_scene PUBLIC nwb_ecs)
