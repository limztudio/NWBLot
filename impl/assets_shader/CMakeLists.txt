nwb_declare_static_library(nwb_assets_shader)
target_sources(nwb_assets_shader PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/shader_asset.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/shader_asset.h"
)
target_link_libraries(nwb_assets_shader PUBLIC nwb_assets)
