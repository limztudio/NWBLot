nwb_declare_static_library(nwb_ecs_geometry)
target_sources(nwb_ecs_geometry PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/geometry_system.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_geometry.h"
    "${CMAKE_CURRENT_LIST_DIR}/geometry_system.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_geometry.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_geometry_buffer_upload.h"
)
target_link_libraries(nwb_ecs_geometry PUBLIC
    nwb_ecs
    nwb_assets
    nwb_graphics
)

