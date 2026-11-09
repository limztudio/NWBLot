nwb_declare_static_library(nwb_assets_sampler_types)
nwb_target_adhd_sources(nwb_assets_sampler_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/types.adhd"
)
target_link_libraries(nwb_assets_sampler_types PUBLIC nwb_assets)

nwb_declare_static_library(nwb_assets_sampler)
nwb_target_adhd_sources(nwb_assets_sampler
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/asset.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
)
target_link_libraries(nwb_assets_sampler PUBLIC nwb_assets nwb_assets_sampler_types nwb_graphics)

nwb_declare_static_library(nwb_assets_sampler_loader)
nwb_target_adhd_sources(nwb_assets_sampler_loader
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/loader.adhd"
)
target_link_libraries(nwb_assets_sampler_loader PUBLIC nwb_assets_sampler nwb_graphics)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_sampler_cook)
    nwb_target_adhd_sources(nwb_assets_sampler_cook
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook.adhd"
    )
    target_link_libraries(nwb_assets_sampler_cook PUBLIC nwb_assets nwb_assets_cook nwb_assets_sampler_types nwb_graphics nwb_metascript)
    target_compile_definitions(nwb_assets_sampler_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_sampler_volume_entry)
    nwb_target_adhd_sources(nwb_assets_sampler_volume_entry
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/volume_entry.adhd"
    )
    target_link_libraries(nwb_assets_sampler_volume_entry PRIVATE nwb_assets_sampler_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_sampler_volume_entry PRIVATE NWB_COOK=1)
endif()
