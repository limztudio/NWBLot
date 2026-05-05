nwb_declare_static_library(nwb_assets_shader)
target_sources(nwb_assets_shader PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/shader_asset.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/shader_asset.h"
)
target_link_libraries(nwb_assets_shader PUBLIC nwb_assets)

if(TARGET nwb_graphics_cook)
    nwb_declare_static_library(nwb_assets_shader_cook)
    target_sources(nwb_assets_shader_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/shader_asset.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/shader_cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/shader_asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/shader_cook.h"
    )
    target_link_libraries(nwb_assets_shader_cook PUBLIC nwb_assets nwb_graphics_cook nwb_metascript)
    target_compile_definitions(nwb_assets_shader_cook PRIVATE NWB_COOK=1)
endif()
