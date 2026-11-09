nwb_declare_static_library(nwb_assets_texture_types)
nwb_target_adhd_sources(nwb_assets_texture_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/texture_binary_codec.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/validation.adhd"
)
target_link_libraries(nwb_assets_texture_types PUBLIC nwb_assets)

nwb_declare_static_library(nwb_assets_texture)
nwb_target_adhd_sources(nwb_assets_texture
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/asset.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
)
target_link_libraries(nwb_assets_texture PUBLIC nwb_assets nwb_assets_texture_types)

nwb_declare_static_library(nwb_assets_texture_loader)
nwb_target_adhd_sources(nwb_assets_texture_loader
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/loader.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/texture_mip_decoder.adhd"
)
target_link_libraries(nwb_assets_texture_loader PUBLIC nwb_assets nwb_assets_texture nwb_assets_texture_types nwb_graphics)
target_link_libraries(nwb_assets_texture_loader PRIVATE nwb::basis_universal)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_texture_cook)
    nwb_target_adhd_sources(nwb_assets_texture_cook
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook.adhd"
    )
    target_link_libraries(nwb_assets_texture_cook PUBLIC nwb_assets nwb_assets_cook nwb_assets_texture_types nwb_metascript)
    target_compile_definitions(nwb_assets_texture_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_texture_volume_entry)
    nwb_target_adhd_sources(nwb_assets_texture_volume_entry
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/volume_entry.adhd"
    )
    target_link_libraries(nwb_assets_texture_volume_entry PRIVATE nwb_assets_texture_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_texture_volume_entry PRIVATE NWB_COOK=1)
endif()
