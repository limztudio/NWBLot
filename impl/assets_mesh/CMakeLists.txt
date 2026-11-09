nwb_declare_static_library(nwb_assets_mesh_types)
nwb_target_adhd_sources(nwb_assets_mesh_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet_bounds.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet_builder.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet_metrics.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet_ref_encoding.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet_refs.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet_score_state.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet_scoring.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/cook_meshlets.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/cook_metadata_value.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/cook_solid_topology.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/cook_stream_reorder.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/cook_types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/mesh_binary_codec.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/meshlet_cone_packing.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_decoding.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/payload_types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/payload_validation.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/skin_binary_codec.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/skin_types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/stream_validation.adhd"
)
target_link_libraries(nwb_assets_mesh_types PUBLIC nwb_assets nwb_assets_subsystem_types nwb_assets_skeleton_types)

nwb_declare_static_library(nwb_assets_mesh)
nwb_target_adhd_sources(nwb_assets_mesh
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/asset.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/binary_payload_io.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_encode.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_validation.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/meshlet_triangle_indices.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/meshlet_triangle_visit.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/meshlet_vertex_attributes.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/skin_asset.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
)
target_link_libraries(nwb_assets_mesh PUBLIC nwb_assets nwb_assets_subsystem nwb_assets_skeleton nwb_mesh nwb_assets_skeleton_types nwb_assets_mesh_types)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_mesh_cook)
    nwb_target_adhd_sources(nwb_assets_mesh_cook
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet_candidates.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet_growth.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook_meshlet_triangles.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook_metadata.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/skin_cook.adhd"
    )
    target_link_libraries(nwb_assets_mesh_cook PUBLIC nwb_assets nwb_assets_subsystem_cook nwb_assets_skeleton_cook nwb_mesh nwb_metascript nwb_assets_skeleton_types nwb_assets_mesh_types nwb_cpu_task)
    target_compile_definitions(nwb_assets_mesh_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_mesh_volume_entries)
    nwb_target_adhd_sources(nwb_assets_mesh_volume_entries
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/volume_entries.adhd"
    )
    target_link_libraries(nwb_assets_mesh_volume_entries PRIVATE nwb_assets_mesh_cook nwb_assets_subsystem_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_mesh_volume_entries PRIVATE NWB_COOK=1)
endif()
