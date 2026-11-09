nwb_declare_static_library(nwb_assets_scene_types)
nwb_target_adhd_sources(nwb_assets_scene_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/component_asset_binary_codec.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/component_assets.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/component_binary.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/scene_asset_binary_codec.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/scene_assets.adhd"
)
target_link_libraries(nwb_assets_scene_types PUBLIC nwb_assets nwb_assets_subsystem_types nwb_ecs nwb_ecs_scene)

nwb_declare_static_library(nwb_assets_scene)
nwb_target_adhd_sources(nwb_assets_scene
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/asset.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/components.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
)
target_link_libraries(nwb_assets_scene PUBLIC nwb_assets nwb_assets_subsystem nwb_ecs nwb_ecs_scene nwb_assets_scene_types)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_scene_cook)
    nwb_target_adhd_sources(nwb_assets_scene_cook
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/cook.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook_component_metadata.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
    )
    target_link_libraries(nwb_assets_scene_cook PUBLIC nwb_assets nwb_assets_subsystem_cook nwb_ecs nwb_ecs_scene nwb_assets_scene_types nwb_metascript)
    target_compile_definitions(nwb_assets_scene_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_scene_volume_entry)
    nwb_target_adhd_sources(nwb_assets_scene_volume_entry
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/volume_entry.adhd"
    )
    target_link_libraries(nwb_assets_scene_volume_entry PRIVATE
        nwb_assets_scene_cook
        nwb_assets_volume_cook
    )
    target_compile_definitions(nwb_assets_scene_volume_entry PRIVATE NWB_COOK=1)
endif()
