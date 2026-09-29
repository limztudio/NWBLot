nwb_declare_static_library(nwb_font_atlas_bake)
target_sources(nwb_font_atlas_bake PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/bake.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/font_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/sdf_raster.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/atlas_pack.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/kerning.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/asset_writer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/asset_metadata.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/asset_metadata.h"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/bake.h"
    "${CMAKE_CURRENT_LIST_DIR}/font_source.h"
)
target_link_libraries(nwb_font_atlas_bake PUBLIC nwb_assets_font_atlas PRIVATE nwb_assets_font nwb_assets_font_atlas_source nwb::freetype nwb_logclient)

nwb_declare_executable(nwb_font_atlas)
target_sources(nwb_font_atlas PRIVATE "${CMAKE_CURRENT_LIST_DIR}/main.cpp" "${CMAKE_CURRENT_LIST_DIR}/command_line.cpp")
target_link_libraries(nwb_font_atlas PRIVATE nwb_font_atlas_bake nwb_common nwb_alloc nwb_logclient nwb::cli11_headers)
