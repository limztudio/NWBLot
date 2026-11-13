nwb_declare_static_library(nwb_assets_font_atlas_types)
nwb_target_adhd_sources(nwb_assets_font_atlas_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/binary_glyph_reader.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/constants.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/gdef_validation.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/glyph_validation.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/kern_validation.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/ot_table_reader.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/positioning_table_constants.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/positioning_validation.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/types.adhd"
)
target_link_libraries(nwb_assets_font_atlas_types PUBLIC nwb_assets nwb_assets_font_types)

nwb_declare_static_library(nwb_assets_font_atlas)
nwb_target_adhd_sources(nwb_assets_font_atlas
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/asset.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/model.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/payload_codec.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
)
target_link_libraries(nwb_assets_font_atlas PUBLIC nwb_assets_font nwb_assets_font_atlas_types)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_font_atlas_cook)
    nwb_target_adhd_sources(nwb_assets_font_atlas_cook
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/cook.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/payload_codec.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
    )
    target_link_libraries(nwb_assets_font_atlas_cook PUBLIC nwb_assets_font_cook nwb_assets_font_atlas_types nwb_metascript)
    target_compile_definitions(nwb_assets_font_atlas_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_font_atlas_volume_entry)
    nwb_target_adhd_sources(nwb_assets_font_atlas_volume_entry
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/volume_entry.adhd"
    )
    target_link_libraries(nwb_assets_font_atlas_volume_entry PRIVATE nwb_assets_font_atlas_cook nwb_assets_font_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_font_atlas_volume_entry PRIVATE NWB_COOK=1)
endif()
