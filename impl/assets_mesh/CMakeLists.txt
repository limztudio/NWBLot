nwb_declare_static_library(nwb_assets_mesh)
target_sources(nwb_assets_mesh PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_asset_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/mesh_asset_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_types.h"
    "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_validation.h"
    "${CMAKE_CURRENT_LIST_DIR}/mesh_payload_types.h"
    "${CMAKE_CURRENT_LIST_DIR}/mesh_payload_validation.h"
    "${CMAKE_CURRENT_LIST_DIR}/mesh_asset_runtime_validation.inl"
    "${CMAKE_CURRENT_LIST_DIR}/mesh_asset_binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/mesh_binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/mesh_asset.h"
)
target_link_libraries(nwb_assets_mesh PUBLIC nwb_assets nwb_mesh nwb_skinned_mesh_types)

if(NWB_BUILD_RESOURCE_COOKER)
    nwb_declare_static_library(nwb_assets_mesh_cook)
    target_sources(nwb_assets_mesh_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_asset_runtime.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_asset_cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/mesh_asset_runtime.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/mesh_asset_cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_binary_payload.h"
        "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_types.h"
        "${CMAKE_CURRENT_LIST_DIR}/skinned_mesh_validation.h"
        "${CMAKE_CURRENT_LIST_DIR}/mesh_payload_types.h"
        "${CMAKE_CURRENT_LIST_DIR}/mesh_payload_validation.h"
        "${CMAKE_CURRENT_LIST_DIR}/mesh_asset_runtime_validation.inl"
        "${CMAKE_CURRENT_LIST_DIR}/mesh_asset_binary_payload.h"
        "${CMAKE_CURRENT_LIST_DIR}/mesh_binary_payload.h"
        "${CMAKE_CURRENT_LIST_DIR}/mesh_asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/mesh_asset_cook.h"
        "${CMAKE_CURRENT_LIST_DIR}/mesh_asset_cook_metadata.inl"
        "${CMAKE_CURRENT_LIST_DIR}/mesh_asset_cook_meshlet.inl"
        "${CMAKE_CURRENT_LIST_DIR}/mesh_asset_cook_source.inl"
    )
    target_link_libraries(nwb_assets_mesh_cook PUBLIC nwb_assets nwb_mesh nwb_metascript nwb_skinned_mesh_types)
    target_compile_definitions(nwb_assets_mesh_cook PRIVATE NWB_COOK=1)
endif()
