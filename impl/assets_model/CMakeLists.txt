nwb_declare_static_library(nwb_assets_model)
target_sources(nwb_assets_model PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/cook.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
)
target_link_libraries(nwb_assets_model PUBLIC nwb_assets nwb_assets_material nwb_assets_mesh nwb_assets_skeleton)

if(NWB_BUILD_RESOURCE_COOKER)
    nwb_declare_static_library(nwb_assets_model_cook)
    target_sources(nwb_assets_model_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    )
    target_link_libraries(nwb_assets_model_cook PUBLIC nwb_assets nwb_assets_material nwb_assets_mesh_cook nwb_assets_skeleton_cook)
    target_compile_definitions(nwb_assets_model_cook PRIVATE NWB_COOK=1)
endif()
