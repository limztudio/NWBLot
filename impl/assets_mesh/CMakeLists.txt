nwb_declare_static_library(nwb_assets_mesh)
target_sources(nwb_assets_mesh PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/skin_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/skin_asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/skin_binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/skin_types.h"
    "${CMAKE_CURRENT_LIST_DIR}/skin_validation.h"
    "${CMAKE_CURRENT_LIST_DIR}/geometry_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/payload_types.h"
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_payload_packing.h"
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_decode.h"
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_encode.h"
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_codec.h"
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_range_validation.inl"
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_validation.inl"
    "${CMAKE_CURRENT_LIST_DIR}/payload_validation.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_validation.inl"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_validation_diagnostics.inl"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_validation_streams.inl"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_validation_meshlets.inl"
    "${CMAKE_CURRENT_LIST_DIR}/binary_payload_io.h"
    "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/asset.h"
)
target_link_libraries(nwb_assets_mesh PUBLIC nwb_assets nwb_assets_skeleton nwb_mesh nwb_assets_skeleton_types)

if(NWB_BUILD_RESOURCE_COOKER)
    nwb_declare_static_library(nwb_assets_mesh_cook)
    target_sources(nwb_assets_mesh_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/skin_runtime.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/skin_cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/skin_asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/skin_cook.h"
        "${CMAKE_CURRENT_LIST_DIR}/skin_binary_payload.h"
        "${CMAKE_CURRENT_LIST_DIR}/skin_types.h"
        "${CMAKE_CURRENT_LIST_DIR}/skin_validation.h"
        "${CMAKE_CURRENT_LIST_DIR}/geometry_payload.h"
        "${CMAKE_CURRENT_LIST_DIR}/payload_types.h"
        "${CMAKE_CURRENT_LIST_DIR}/meshlet_payload_packing.h"
        "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_decode.h"
        "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_encode.h"
        "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_codec.h"
        "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_range_validation.inl"
        "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_validation.inl"
        "${CMAKE_CURRENT_LIST_DIR}/payload_validation.h"
        "${CMAKE_CURRENT_LIST_DIR}/runtime_validation.inl"
        "${CMAKE_CURRENT_LIST_DIR}/runtime_validation_diagnostics.inl"
        "${CMAKE_CURRENT_LIST_DIR}/runtime_validation_streams.inl"
        "${CMAKE_CURRENT_LIST_DIR}/runtime_validation_meshlets.inl"
        "${CMAKE_CURRENT_LIST_DIR}/binary_payload_io.h"
        "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
        "${CMAKE_CURRENT_LIST_DIR}/asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook_metadata.inl"
        "${CMAKE_CURRENT_LIST_DIR}/cook_stream_reorder.inl"
        "${CMAKE_CURRENT_LIST_DIR}/cook_ref_encoding.inl"
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet.inl"
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet_common.inl"
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet_metrics.inl"
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet_precompute.inl"
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet_bounds.inl"
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet_scoring.inl"
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet_candidates.inl"
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet_frontier.inl"
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet_refs.inl"
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet_build.inl"
        "${CMAKE_CURRENT_LIST_DIR}/cook_source.inl"
    )
    target_link_libraries(nwb_assets_mesh_cook PUBLIC nwb_assets nwb_assets_skeleton_cook nwb_mesh nwb_metascript nwb_assets_skeleton_types)
    target_link_libraries(nwb_assets_mesh_cook PRIVATE nwb_assets_volume_cook)
    target_compile_definitions(nwb_assets_mesh_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_mesh_volume_entries)
    target_sources(nwb_assets_mesh_volume_entries PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/volume_entries.cpp"
    )
    target_link_libraries(nwb_assets_mesh_volume_entries PRIVATE nwb_assets_mesh_cook nwb_assets_volume_cook)
    target_compile_definitions(nwb_assets_mesh_volume_entries PRIVATE NWB_COOK=1)
endif()
