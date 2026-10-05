global_declare_static_library(nwb_font_builder_bake)
target_sources(nwb_font_builder_bake PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/bake.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/font_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/sdf_raster.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/atlas_pack.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/atlas_compact.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/atlas_compact.h"
    "${CMAKE_CURRENT_LIST_DIR}/kerning.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/asset_writer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/asset_metadata.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/asset_metadata.h"
    "${CMAKE_CURRENT_LIST_DIR}/prepared_font.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/prepared_font.h"
    "${CMAKE_CURRENT_LIST_DIR}/source_input.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/source_input.h"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/bake.h"
    "${CMAKE_CURRENT_LIST_DIR}/font_source.h"
)
target_link_libraries(nwb_font_builder_bake PUBLIC nwb_assets_font_atlas PRIVATE nwb_assets_font nwb::freetype nwb_logclient)

nwb_declare_executable(nwb_font_builder)
target_sources(nwb_font_builder PRIVATE "${CMAKE_CURRENT_LIST_DIR}/main.cpp" "${CMAKE_CURRENT_LIST_DIR}/command_line.cpp")
target_link_libraries(nwb_font_builder PRIVATE nwb_font_builder_bake nwb_common nwb_alloc nwb_logclient nwb::cli11_headers)
