nwb_declare_static_library(nwb_assets_physics_data_types)
nwb_target_adhd_sources(nwb_assets_physics_data_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/physics_assets.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/physics_asset_binary_codec.adhd"
)
target_link_libraries(nwb_assets_physics_data_types PUBLIC nwb_assets nwb_physics)

nwb_declare_static_library(nwb_assets_physics_types)
nwb_target_adhd_sources(nwb_assets_physics_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/physics_subsystem.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/components.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/physics_component_binary_codec.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/subsystem_component_codec.adhd"
)
target_link_libraries(nwb_assets_physics_types PUBLIC nwb_assets nwb_ecs nwb_ecs_scene nwb_assets_subsystem_types nwb_assets_shader_types nwb_assets_scene_types nwb_assets_physics_data_types nwb_ecs_physics_components)

nwb_declare_static_library(nwb_assets_physics)
nwb_target_adhd_sources(nwb_assets_physics
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime_physics_assets.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime_components.adhd"
)
target_link_libraries(nwb_assets_physics PUBLIC nwb_assets nwb_assets_subsystem nwb_assets_shader nwb_assets_scene nwb_ecs_physics_components nwb_assets_physics_types)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_physics_cook)
    nwb_target_adhd_sources(nwb_assets_physics_cook
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/runtime_physics_assets.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/runtime_components.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook_physics_assets.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook_components.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook_subsystem_components.adhd"
    )
    target_link_libraries(nwb_assets_physics_cook PUBLIC nwb_assets nwb_assets_subsystem_cook nwb_assets_scene_cook nwb_assets_mesh_cook nwb_ecs_physics_components nwb_assets_physics_types)
    target_compile_definitions(nwb_assets_physics_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_physics_volume_entry)
    nwb_target_adhd_sources(nwb_assets_physics_volume_entry
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/volume_entry.adhd"
    )
    target_link_libraries(nwb_assets_physics_volume_entry PRIVATE nwb_assets_physics_cook nwb_assets_physics_types nwb_assets_subsystem_cook nwb_assets_scene_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_physics_volume_entry PRIVATE NWB_COOK=1)
endif()
