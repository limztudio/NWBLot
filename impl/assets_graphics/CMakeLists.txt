nwb_declare_interface_library(nwb_assets_graphics)
target_link_libraries(nwb_assets_graphics INTERFACE
    nwb_assets_geometry
    nwb_assets_material
    nwb_assets_shader
)

if(NWB_BUILD_RESOURCE_COOKER)
    nwb_declare_static_library(nwb_assets_graphics_cook)
    target_sources(nwb_assets_graphics_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/graphics_asset_cooker.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/graphics_asset_cooker.h"
    )
    target_link_libraries(nwb_assets_graphics_cook PUBLIC nwb_assets)
    target_link_libraries(nwb_assets_graphics_cook PRIVATE
        nwb_assets_geometry_cook
        nwb_assets_material_cook
        nwb_assets_shader_cook
        nwb_graphics
        nwb_metascript
        nwb_filesystem
    )
    target_compile_definitions(nwb_assets_graphics_cook PRIVATE NWB_COOK=1)
endif()
