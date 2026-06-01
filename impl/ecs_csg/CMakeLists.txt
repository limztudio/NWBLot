nwb_declare_static_library(nwb_ecs_csg)
target_sources(nwb_ecs_csg PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/frame_state.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/shape_registry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/frame_state.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/shape_registry.h"
)
target_link_libraries(nwb_ecs_csg PUBLIC
    nwb_ecs
)

