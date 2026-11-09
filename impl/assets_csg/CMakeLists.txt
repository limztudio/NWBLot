nwb_declare_static_library(nwb_assets_csg)
nwb_target_adhd_sources(nwb_assets_csg
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/asset.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/shape_id.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/shape_module_source.adhd"
)
target_link_libraries(nwb_assets_csg PUBLIC nwb_assets)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_csg_cook)
    nwb_target_adhd_sources(nwb_assets_csg_cook
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/cook.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/shape_module_writer.adhd"
    )
    target_link_libraries(nwb_assets_csg_cook PUBLIC nwb_assets nwb_assets_cook nwb_assets_csg nwb_metascript nwb_filesystem)
    target_compile_definitions(nwb_assets_csg_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_csg_volume_entry)
    nwb_target_adhd_sources(nwb_assets_csg_volume_entry
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/volume_entry.adhd"
    )
    target_link_libraries(nwb_assets_csg_volume_entry PRIVATE nwb_assets_csg_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_csg_volume_entry PRIVATE NWB_COOK=1)
endif()
