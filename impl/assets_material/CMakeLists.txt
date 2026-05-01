nwb_declare_static_library(nwb_assets_material)
target_sources(nwb_assets_material PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/material_asset.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/material_asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/material_shader_stage_names.h"
)
target_link_libraries(nwb_assets_material PUBLIC nwb_assets nwb_assets_shader)
