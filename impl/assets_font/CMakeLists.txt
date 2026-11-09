nwb_declare_static_library(nwb_assets_font_types)
nwb_target_adhd_sources(nwb_assets_font_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/font_binary_codec.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/prepared_font_binary_codec.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/sfnt_directory.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/types.adhd"
)
target_link_libraries(nwb_assets_font_types PUBLIC nwb_assets)

nwb_declare_static_library(nwb_assets_font)
nwb_target_adhd_sources(nwb_assets_font
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/asset.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/prepared_source.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
)
target_link_libraries(nwb_assets_font PUBLIC nwb_assets nwb_assets_font_types PRIVATE nwb::freetype)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_font_cook)
    nwb_target_adhd_sources(nwb_assets_font_cook
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook.adhd"
    )
    target_link_libraries(nwb_assets_font_cook PUBLIC nwb_assets nwb_assets_cook nwb_assets_font_types nwb_metascript PRIVATE nwb::freetype)
    target_compile_definitions(nwb_assets_font_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_font_volume_entry)
    nwb_target_adhd_sources(nwb_assets_font_volume_entry
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/volume_entry.adhd"
    )
    target_link_libraries(nwb_assets_font_volume_entry PRIVATE nwb_assets_font_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_font_volume_entry PRIVATE NWB_COOK=1)
endif()
