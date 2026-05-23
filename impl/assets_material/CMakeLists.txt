nwb_declare_static_library(nwb_assets_material)
target_sources(nwb_assets_material PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/material_asset_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/material_asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/material_binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/material_shader_stage_names.h"
)
target_link_libraries(nwb_assets_material PUBLIC nwb_assets nwb_graphics)

if(NWB_BUILD_RESOURCE_COOKER)
    nwb_declare_static_library(nwb_assets_material_cook)
    target_sources(nwb_assets_material_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/material_asset_runtime.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/material_asset_cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/material_bind.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/material_asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/material_asset_cook.h"
        "${CMAKE_CURRENT_LIST_DIR}/material_bind.h"
        "${CMAKE_CURRENT_LIST_DIR}/material_binary_payload.h"
        "${CMAKE_CURRENT_LIST_DIR}/material_cook_types.h"
        "${CMAKE_CURRENT_LIST_DIR}/material_shader_stage_names.h"
    )
    target_link_libraries(nwb_assets_material_cook PUBLIC nwb_assets nwb_assets_shader_cook nwb_graphics)
    target_compile_definitions(nwb_assets_material_cook PRIVATE NWB_COOK=1)
endif()
