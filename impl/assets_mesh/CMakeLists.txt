set(NWB_ASSETS_MESH_RUNTIME_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/skin_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/skin_asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/skin_binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/skin_types.h"
    "${CMAKE_CURRENT_LIST_DIR}/skin_validation.h"
    "${CMAKE_CURRENT_LIST_DIR}/geometry_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_constants.h"
    "${CMAKE_CURRENT_LIST_DIR}/payload_types.h"
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_payload_packing.h"
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_decode.h"
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_triangle_visit.h"
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_triangle_indices.h"
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_encode.h"
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_codec.h"
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_validation.h"
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_validation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/payload_validation.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_validation.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_validation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_validation_diagnostics.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_validation_diagnostics.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/binary_payload_io.h"
    "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/arena_names.h"
    "${CMAKE_CURRENT_LIST_DIR}/asset.h"
)

nwb_declare_static_library(nwb_assets_mesh)
target_sources(nwb_assets_mesh PRIVATE
    ${NWB_ASSETS_MESH_RUNTIME_SOURCES}
)
target_link_libraries(nwb_assets_mesh PUBLIC nwb_assets nwb_assets_skeleton nwb_mesh nwb_assets_skeleton_types)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_mesh_cook)
    target_sources(nwb_assets_mesh_cook PRIVATE
        ${NWB_ASSETS_MESH_RUNTIME_SOURCES}
        "${CMAKE_CURRENT_LIST_DIR}/skin_cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/skin_cook.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook_metadata.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook_metadata.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_source_streams.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook_source_streams.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_stream_reorder.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook_stream_reorder.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_ref_encoding.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook_ref_encoding.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlets.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlets.cpp"
    )
    target_link_libraries(nwb_assets_mesh_cook PUBLIC nwb_assets nwb_assets_skeleton_cook nwb_mesh nwb_metascript nwb_assets_skeleton_types nwb_cpu_task)
    target_compile_definitions(nwb_assets_mesh_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_mesh_volume_entries)
    target_sources(nwb_assets_mesh_volume_entries PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/volume_entries.cpp"
    )
    target_link_libraries(nwb_assets_mesh_volume_entries PRIVATE nwb_assets_mesh_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_mesh_volume_entries PRIVATE NWB_COOK=1)
endif()
