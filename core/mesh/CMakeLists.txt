nwb_declare_interface_library(nwb_mesh)
target_sources(nwb_mesh INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/classification.h"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
)
target_link_libraries(nwb_mesh INTERFACE nwb_common nwb_alloc)
