nwb_declare_static_library(nwb_assets_geometry)
target_sources(nwb_assets_geometry PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/deformable_geometry_asset.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/geometry_asset.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_geometry_asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_geometry_types.h"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_geometry_validation.h"
    "${CMAKE_CURRENT_LIST_DIR}/geometry_binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/geometry_asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/geometry_class.h"
)
target_link_libraries(nwb_assets_geometry PUBLIC nwb_assets nwb_geometry)
