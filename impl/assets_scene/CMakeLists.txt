nwb_declare_static_library(nwb_assets_scene)
target_sources(nwb_assets_scene PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/arena_names.h"
    "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
)
target_link_libraries(nwb_assets_scene PUBLIC nwb_assets nwb_assets_subsystem)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_scene_cook)
    target_sources(nwb_assets_scene_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/arena_names.h"
        "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    )
    target_link_libraries(nwb_assets_scene_cook PUBLIC nwb_assets nwb_assets_subsystem_cook nwb_metascript)
    target_compile_definitions(nwb_assets_scene_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_scene_volume_entry)
    target_sources(nwb_assets_scene_volume_entry PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/volume_entry.cpp"
    )
    target_link_libraries(nwb_assets_scene_volume_entry PRIVATE
        nwb_assets_scene_cook
        nwb_assets_csg_cook
        nwb_assets_material_cook
        nwb_assets_mesh_cook
        nwb_assets_model_cook
        nwb_assets_volume_cook
    )
    target_compile_definitions(nwb_assets_scene_volume_entry PRIVATE NWB_COOK=1)
endif()
