nwb_declare_static_library(nwb_ui_text)
target_sources(nwb_ui_text PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/types.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/font.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/shaper.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/layout.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/atlas.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/baked_atlas.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/service*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/glyph_visibility.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
)
target_link_libraries(nwb_ui_text PUBLIC nwb_ui nwb_assets_font_atlas PRIVATE nwb::freetype nwb::harfbuzz)
nwb_target_adhd_sources(nwb_ui_text
    NAMESPACE NWB::Impl::Ui
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
)
