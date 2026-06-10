nwb_declare_interface_library(nwb_assets_skeleton_types)
target_sources(nwb_assets_skeleton_types INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/joint_types.h"
)

nwb_declare_static_library(nwb_assets_skeleton)
target_sources(nwb_assets_skeleton PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/joint_types.h"
    "${CMAKE_CURRENT_LIST_DIR}/asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
)
target_link_libraries(nwb_assets_skeleton PUBLIC nwb_assets nwb_assets_skeleton_types)

if(NWB_BUILD_RESOURCE_COOKER)
    nwb_declare_static_library(nwb_assets_skeleton_cook)
    target_sources(nwb_assets_skeleton_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/joint_types.h"
        "${CMAKE_CURRENT_LIST_DIR}/asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook_matrix.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    )
    target_link_libraries(nwb_assets_skeleton_cook PUBLIC nwb_assets nwb_assets_skeleton_types)
    target_link_libraries(nwb_assets_skeleton_cook PRIVATE nwb_assets_volume_cook)
    target_compile_definitions(nwb_assets_skeleton_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_skeleton_volume_entry)
    target_sources(nwb_assets_skeleton_volume_entry PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/volume_entry.cpp"
    )
    target_link_libraries(nwb_assets_skeleton_volume_entry PRIVATE nwb_assets_skeleton_cook nwb_assets_volume_cook)
    target_compile_definitions(nwb_assets_skeleton_volume_entry PRIVATE NWB_COOK=1)
endif()
