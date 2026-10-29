nwb_declare_static_library(nwb_assets_scene_types)
nwb_target_adhd_sources(nwb_assets_scene_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/component_assets.adhd"
)
target_link_libraries(nwb_assets_scene_types PUBLIC nwb_assets nwb_ecs nwb_ecs_scene)

nwb_declare_static_library(nwb_assets_scene)
target_sources(nwb_assets_scene PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/arena_names.h"
    "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/components.h"
    "${CMAKE_CURRENT_LIST_DIR}/active_scene_subsystems.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime_components.cpp"
)
target_link_libraries(nwb_assets_scene PUBLIC nwb_assets nwb_assets_subsystem nwb_ecs nwb_ecs_scene nwb_assets_scene_types)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_scene_cook)
    target_sources(nwb_assets_scene_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/arena_names.h"
        "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
        "${CMAKE_CURRENT_LIST_DIR}/components.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_components.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_metadata.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/runtime_components.cpp"
    )
    target_link_libraries(nwb_assets_scene_cook PUBLIC nwb_assets nwb_assets_subsystem_cook nwb_ecs nwb_ecs_scene nwb_assets_scene_types nwb_metascript)
    target_compile_definitions(nwb_assets_scene_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_scene_volume_entry)
    target_sources(nwb_assets_scene_volume_entry PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/volume_entry.cpp"
    )
    target_link_libraries(nwb_assets_scene_volume_entry PRIVATE
        nwb_assets_scene_cook
        nwb_assets_volume_cook
    )
    target_compile_definitions(nwb_assets_scene_volume_entry PRIVATE NWB_COOK=1)
endif()
