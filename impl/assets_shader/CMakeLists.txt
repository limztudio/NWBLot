nwb_declare_static_library(nwb_assets_shader)
target_sources(nwb_assets_shader PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/shader_asset_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/shader_asset.h"
)
target_link_libraries(nwb_assets_shader PUBLIC nwb_assets)

nwb_declare_interface_library(nwb_assets_shader_loader)
target_sources(nwb_assets_shader_loader INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/shader_asset_loader.h"
)
target_link_libraries(nwb_assets_shader_loader INTERFACE
    nwb_assets_shader
    nwb_graphics
)

if(NWB_BUILD_RESOURCE_COOKER)
    nwb_declare_static_library(nwb_assets_shader_cook)
    target_sources(nwb_assets_shader_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/shader_asset_runtime.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/shader_asset_cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/shader_cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/shader_asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/shader_cook.h"
    )
    target_link_libraries(nwb_assets_shader_cook PUBLIC nwb_assets nwb_metascript)
    target_compile_definitions(nwb_assets_shader_cook PRIVATE
        NWB_COOK=1
        NWB_SLANGC_EXECUTABLE="${NWB_SLANGC_EXECUTABLE}"
    )
endif()
