nwb_declare_static_library(nwb_filesystem)
target_sources(nwb_filesystem PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/filesystem.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/filesystem.h"
    "${CMAKE_CURRENT_LIST_DIR}/volume_naming.h"
)
target_link_libraries(nwb_filesystem PUBLIC nwb_common nwb_alloc)
