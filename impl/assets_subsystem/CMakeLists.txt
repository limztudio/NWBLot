nwb_declare_static_library(nwb_assets_subsystem_types)
nwb_target_adhd_sources(nwb_assets_subsystem_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/subsystem_binary_codec.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/types.adhd"
)
target_link_libraries(nwb_assets_subsystem_types PUBLIC nwb_assets)

nwb_declare_static_library(nwb_assets_subsystem)
nwb_target_adhd_sources(nwb_assets_subsystem
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/asset.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
)
target_link_libraries(nwb_assets_subsystem PUBLIC nwb_assets nwb_assets_subsystem_types)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_subsystem_cook)
    nwb_target_adhd_sources(nwb_assets_subsystem_cook
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook.adhd"
    )
    target_link_libraries(nwb_assets_subsystem_cook PUBLIC nwb_assets nwb_assets_cook nwb_assets_subsystem_types nwb_metascript)
    target_compile_definitions(nwb_assets_subsystem_cook PRIVATE NWB_COOK=1)
endif()
