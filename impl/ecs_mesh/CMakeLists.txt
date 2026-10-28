nwb_declare_static_library(nwb_ecs_mesh)
target_sources(nwb_ecs_mesh PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/buffer_upload.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/resource_names.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/mesh.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/mesh_requests.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/mesh_requests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/system.h"
)
nwb_target_adhd_sources(nwb_ecs_mesh
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/runtime/handle.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/components.adhd"
    DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/runtime/handle.adhd"
)
target_link_libraries(nwb_ecs_mesh PUBLIC nwb_ecs nwb_assets nwb_graphics nwb_assets_skeleton_types)

add_subdirectory(skinning)
