nwb_declare_static_library(nwb_assets_skeleton)
target_sources(nwb_assets_skeleton PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
)
target_link_libraries(nwb_assets_skeleton PUBLIC nwb_assets nwb_skinned_mesh_types)

if(NWB_BUILD_RESOURCE_COOKER)
    nwb_declare_static_library(nwb_assets_skeleton_cook)
    target_sources(nwb_assets_skeleton_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    )
    target_link_libraries(nwb_assets_skeleton_cook PUBLIC nwb_assets nwb_skinned_mesh_types)
    target_compile_definitions(nwb_assets_skeleton_cook PRIVATE NWB_COOK=1)
endif()
