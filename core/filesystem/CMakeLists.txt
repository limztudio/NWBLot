nwb_declare_static_library(nwb_filesystem)
target_sources(nwb_filesystem PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/volume_naming.h"
    "${CMAKE_CURRENT_LIST_DIR}/arena_names.h"
)
target_link_libraries(nwb_filesystem PUBLIC nwb_common nwb_alloc)
