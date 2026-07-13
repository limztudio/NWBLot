nwb_declare_static_library(nwb_mesh)
target_sources(nwb_mesh PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/tangent_frame_rebuild.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame_math.h"
    "${CMAKE_CURRENT_LIST_DIR}/classification.h"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/tangent_frame_rebuild.h"
    "${CMAKE_CURRENT_LIST_DIR}/arena_names.h"
)
target_link_libraries(nwb_mesh PUBLIC nwb_common nwb_alloc)
