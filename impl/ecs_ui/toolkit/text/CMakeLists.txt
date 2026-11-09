nwb_declare_static_library(nwb_ui_text)
nwb_target_adhd_sources(nwb_ui_text
    NAMESPACE "NWB::Impl::Ui"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/atlas.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/baked_atlas.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/font.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/glyph_page.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/glyph_visibility.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/layout.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/sdf_page.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/service.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/shaper.adhd"
)
target_link_libraries(nwb_ui_text PUBLIC nwb_ui nwb_assets_font_atlas PRIVATE nwb::freetype nwb::harfbuzz)
