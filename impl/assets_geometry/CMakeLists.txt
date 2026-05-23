nwb_declare_static_library(nwb_assets_geometry)
target_sources(nwb_assets_geometry PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_asset_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/geometry_asset_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_payload_logging.h"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_types.h"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_validation.h"
    "${CMAKE_CURRENT_LIST_DIR}/geometry_binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/geometry_asset.h"
)
target_link_libraries(nwb_assets_geometry PUBLIC nwb_assets nwb_geometry nwb_skinned_geometry_types)

if(NWB_BUILD_RESOURCE_COOKER)
    nwb_declare_static_library(nwb_assets_geometry_cook)
    target_sources(nwb_assets_geometry_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_asset_runtime.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_asset_cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/geometry_asset_runtime.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/geometry_asset_cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_binary_payload.h"
        "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_payload_logging.h"
        "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_tangent_frame_rebuild.h"
        "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_types.h"
        "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_validation.h"
        "${CMAKE_CURRENT_LIST_DIR}/geometry_binary_payload.h"
        "${CMAKE_CURRENT_LIST_DIR}/geometry_asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/geometry_asset_cook.h"
    )
    target_link_libraries(nwb_assets_geometry_cook PUBLIC nwb_assets nwb_geometry nwb_metascript nwb_skinned_geometry_types)
    target_compile_definitions(nwb_assets_geometry_cook PRIVATE NWB_COOK=1)
endif()
