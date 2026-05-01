nwb_declare_interface_library(nwb_assets_graphics)
target_link_libraries(nwb_assets_graphics INTERFACE
    nwb_assets_geometry
    nwb_assets_material
    nwb_assets_shader
)

set(NWB_ASSETS_GRAPHICS_COOK_CODEC_SOURCES
    "${PROJECT_SOURCE_DIR}/impl/assets_geometry/deformable_geometry_asset.cpp"
    "${PROJECT_SOURCE_DIR}/impl/assets_geometry/deformable_operator_shape.cpp"
    "${PROJECT_SOURCE_DIR}/impl/assets_geometry/geometry_asset.cpp"
    "${PROJECT_SOURCE_DIR}/impl/assets_material/material_asset.cpp"
)

if(TARGET nwb_graphics_cook)
    nwb_declare_static_library(nwb_assets_graphics_cook)
    target_sources(nwb_assets_graphics_cook PRIVATE
        ${NWB_ASSETS_GRAPHICS_COOK_CODEC_SOURCES}
        "${CMAKE_CURRENT_LIST_DIR}/graphics_asset_cooker.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/graphics_asset_cooker.h"
    )
    target_link_libraries(nwb_assets_graphics_cook PUBLIC
        nwb_assets_geometry
        nwb_assets_material
        nwb_assets_shader
        nwb_assets
        nwb_graphics
        nwb_graphics_cook
        nwb_metascript
        nwb_filesystem
        nwb_geometry
    )
    target_compile_definitions(nwb_assets_graphics_cook PRIVATE NWB_COOK=1)
endif()
