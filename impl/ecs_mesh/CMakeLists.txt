nwb_declare_static_library(nwb_ecs_mesh)
target_sources(nwb_ecs_mesh PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/mesh_system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_mesh.h"
    "${CMAKE_CURRENT_LIST_DIR}/mesh_system.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_mesh.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_mesh_buffer_upload.h"
)
target_link_libraries(nwb_ecs_mesh PUBLIC
    nwb_ecs
    nwb_assets
    nwb_graphics
)

