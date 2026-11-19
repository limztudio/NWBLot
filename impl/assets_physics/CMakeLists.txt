nwb_declare_static_library(nwb_assets_physics_types)
nwb_target_adhd_sources(nwb_assets_physics_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/physics_subsystem.adhd"
)
target_link_libraries(nwb_assets_physics_types PUBLIC nwb_assets nwb_assets_subsystem_types nwb_assets_shader_types)

nwb_declare_static_library(nwb_assets_physics)
nwb_target_adhd_sources(nwb_assets_physics
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
)
target_link_libraries(nwb_assets_physics PUBLIC nwb_assets nwb_assets_subsystem nwb_assets_shader nwb_assets_physics_types)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_physics_volume_entry)
    nwb_target_adhd_sources(nwb_assets_physics_volume_entry
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/volume_entry.adhd"
    )
    target_link_libraries(nwb_assets_physics_volume_entry PRIVATE nwb_assets_physics_types nwb_assets_subsystem_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_physics_volume_entry PRIVATE NWB_COOK=1)
endif()
