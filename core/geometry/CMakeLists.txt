nwb_declare_static_library(nwb_geometry)
target_sources(nwb_geometry PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_cluster.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/mesh_topology.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/tangent_frame_rebuild.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame_math.h"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_cluster.h"
    "${CMAKE_CURRENT_LIST_DIR}/mesh_topology.h"
    "${CMAKE_CURRENT_LIST_DIR}/tangent_frame_rebuild.h"
)
target_link_libraries(nwb_geometry PUBLIC nwb_common nwb_alloc)
