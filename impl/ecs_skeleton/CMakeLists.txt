nwb_declare_static_library(nwb_ecs_skeleton)
target_sources(nwb_ecs_skeleton PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/runtime_helpers.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
)
nwb_target_adhd_sources(nwb_ecs_skeleton
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/components.adhd"
)
target_link_libraries(nwb_ecs_skeleton PUBLIC nwb_ecs nwb_assets nwb_assets_skeleton_types)
