nwb_declare_static_library(filesystem)
target_sources(filesystem PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/filesystem.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/filesystem.h"
    "${CMAKE_CURRENT_LIST_DIR}/volume_naming.h"
)
target_link_libraries(filesystem PUBLIC alloc)
