nwb_declare_static_library(nwb_assets_ui_skin_types)
nwb_target_adhd_sources(nwb_assets_ui_skin_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/palette_types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/ui_skin_binary_codec.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/validation.adhd"
)
target_link_libraries(nwb_assets_ui_skin_types PUBLIC nwb_assets nwb_assets_subsystem_types nwb_assets_font_types nwb_assets_font_atlas_types nwb_assets_texture_types)

nwb_declare_static_library(nwb_assets_ui_skin)
nwb_target_adhd_sources(nwb_assets_ui_skin
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/asset.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
)
target_link_libraries(nwb_assets_ui_skin PUBLIC nwb_assets nwb_assets_subsystem nwb_assets_ui_skin_types)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_ui_skin_cook)
    nwb_target_adhd_sources(nwb_assets_ui_skin_cook
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook.adhd"
    )
    target_link_libraries(nwb_assets_ui_skin_cook PUBLIC nwb_assets nwb_assets_subsystem_cook nwb_assets_ui_skin_types nwb_metascript)
    target_compile_definitions(nwb_assets_ui_skin_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_ui_skin_volume_entry)
    nwb_target_adhd_sources(nwb_assets_ui_skin_volume_entry
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/volume_entry.adhd"
    )
    target_link_libraries(nwb_assets_ui_skin_volume_entry PRIVATE nwb_assets_ui_skin_cook nwb_assets_texture_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_ui_skin_volume_entry PRIVATE NWB_COOK=1)
endif()
