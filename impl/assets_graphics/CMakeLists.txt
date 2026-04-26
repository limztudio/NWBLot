nwb_declare_static_library(nwb_assets_graphics)
target_sources(nwb_assets_graphics PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/deformable_geometry_asset.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/geometry_asset.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/shader_asset.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/shader_asset_cooker.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/material_asset.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_geometry_asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/deformable_geometry_validation.h"
    "${CMAKE_CURRENT_LIST_DIR}/geometry_asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/shader_asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/shader_stage_names.h"
    "${CMAKE_CURRENT_LIST_DIR}/shader_asset_cooker.h"
    "${CMAKE_CURRENT_LIST_DIR}/material_asset.h"
)
target_link_libraries(nwb_assets_graphics PUBLIC nwb_assets nwb_graphics)

if(TARGET nwb_graphics_cook)
    nwb_declare_static_library(nwb_assets_graphics_cook)
    target_sources(nwb_assets_graphics_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/deformable_gltf_importer.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/deformable_geometry_asset.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/geometry_asset.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/material_asset.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/shader_asset_cooker.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/deformable_gltf_importer.h"
        "${CMAKE_CURRENT_LIST_DIR}/deformable_geometry_asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/deformable_geometry_validation.h"
        "${CMAKE_CURRENT_LIST_DIR}/geometry_asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/material_asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/shader_stage_names.h"
        "${CMAKE_CURRENT_LIST_DIR}/shader_asset_cooker.h"
    )
    target_link_libraries(nwb_assets_graphics_cook PUBLIC nwb_assets nwb_graphics nwb_graphics_cook nwb_metascript nwb_filesystem nwb_geometry)
    target_compile_definitions(nwb_assets_graphics_cook PRIVATE NWB_COOK=1)
endif()
