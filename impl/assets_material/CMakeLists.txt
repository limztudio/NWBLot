nwb_declare_static_library(nwb_assets_material_types)
nwb_target_adhd_sources(nwb_assets_material_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/binary_validation.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/bind_codegen_constants.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/bind_layout_lookup.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/bind_parameter_application.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/bind_schema.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/bind_source_emitter.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/bind_storage.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/bind_token_parser.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/bind_typed_layout.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/bind_types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/bind_value_parser.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/cook_types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/field_types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/layout_validation.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/material_binary_reader.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/material_binary_writer.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/shadow_surface_source_builder.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/types.adhd"
)
target_link_libraries(nwb_assets_material_types PUBLIC nwb_assets nwb_ecs nwb_assets_shader_types)

nwb_declare_static_library(nwb_assets_material)
nwb_target_adhd_sources(nwb_assets_material
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/asset.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
)
target_link_libraries(nwb_assets_material PUBLIC nwb_assets nwb_assets_material_types)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_material_cook)
    nwb_target_adhd_sources(nwb_assets_material_cook
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook_shader_and_asset_emit.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/bind.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook_private.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/material_avboit_shader_generator.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/material_shadow_dispatch.adhd"
    )
    target_link_libraries(nwb_assets_material_cook PUBLIC nwb_assets nwb_assets_cook nwb_assets_material_types nwb_assets_shader_types nwb_metascript)
    target_compile_definitions(nwb_assets_material_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_material_volume_entry)
    nwb_target_adhd_sources(nwb_assets_material_volume_entry
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/volume_entry.adhd"
    )
    target_link_libraries(nwb_assets_material_volume_entry PRIVATE nwb_assets_material_cook nwb_assets_shader_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_material_volume_entry PRIVATE NWB_COOK=1)
endif()
