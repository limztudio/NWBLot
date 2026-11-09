nwb_declare_static_library(nwb_assets_skeleton_types)
nwb_target_adhd_sources(nwb_assets_skeleton_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/joint_types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/joint_validation.adhd"
)
target_link_libraries(nwb_assets_skeleton_types PUBLIC nwb_alloc nwb_ecs)

nwb_declare_static_library(nwb_assets_skeleton)
nwb_target_adhd_sources(nwb_assets_skeleton
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/asset.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
)
target_link_libraries(nwb_assets_skeleton PUBLIC nwb_assets nwb_assets_skeleton_types)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_skeleton_cook)
    nwb_target_adhd_sources(nwb_assets_skeleton_cook
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/cook.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook_matrix.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
    )
    target_link_libraries(nwb_assets_skeleton_cook PUBLIC nwb_assets nwb_assets_cook nwb_assets_skeleton_types)
    target_compile_definitions(nwb_assets_skeleton_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_skeleton_volume_entry)
    nwb_target_adhd_sources(nwb_assets_skeleton_volume_entry
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/volume_entry.adhd"
    )
    target_link_libraries(nwb_assets_skeleton_volume_entry PRIVATE nwb_assets_skeleton_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_skeleton_volume_entry PRIVATE NWB_COOK=1)
endif()
