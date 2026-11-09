nwb_declare_static_library(nwb_assets_model_types)
nwb_target_adhd_sources(nwb_assets_model_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/binary_types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/model_binary_codec.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/types.adhd"
)
target_link_libraries(nwb_assets_model_types PUBLIC nwb_assets nwb_assets_skeleton_types)

nwb_declare_static_library(nwb_assets_model)
nwb_target_adhd_sources(nwb_assets_model
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/asset.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
)
target_link_libraries(nwb_assets_model PUBLIC nwb_assets nwb_assets_model_types nwb_assets_material nwb_assets_mesh nwb_assets_skeleton)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_model_cook)
    nwb_target_adhd_sources(nwb_assets_model_cook
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/cook.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
    )
    target_link_libraries(nwb_assets_model_cook PUBLIC nwb_assets nwb_assets_model_types nwb_assets_material_cook nwb_assets_mesh_cook nwb_assets_skeleton_cook)
    target_compile_definitions(nwb_assets_model_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_model_volume_entry)
    nwb_target_adhd_sources(nwb_assets_model_volume_entry
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/volume_entry.adhd"
    )
    target_link_libraries(nwb_assets_model_volume_entry PRIVATE nwb_assets_model_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_model_volume_entry PRIVATE NWB_COOK=1)
endif()
