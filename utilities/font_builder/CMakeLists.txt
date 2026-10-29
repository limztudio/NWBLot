nwb_declare_static_library(nwb_font_builder_bake)
target_sources(nwb_font_builder_bake PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/bake.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/font_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/sdf_raster.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/atlas_*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/kerning.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/asset_*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/prepared_font.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/source_input.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
)
target_link_libraries(nwb_font_builder_bake PUBLIC nwb_assets_font_atlas PRIVATE nwb_assets_font nwb::freetype nwb_common)

nwb_declare_executable(nwb_font_builder)
target_sources(nwb_font_builder PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/command_line.cpp"
)
target_link_libraries(nwb_font_builder PRIVATE nwb_font_builder_bake nwb_common nwb_alloc nwb_logclient nwb::cli11_headers)
