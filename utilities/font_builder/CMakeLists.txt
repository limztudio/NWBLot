file(GLOB _nwb_font_builder_bake_adhd CONFIGURE_DEPENDS LIST_DIRECTORIES false "${CMAKE_CURRENT_LIST_DIR}/*.adhd")
list(REMOVE_ITEM _nwb_font_builder_bake_adhd
    "${CMAKE_CURRENT_LIST_DIR}/main.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/command_line.adhd"
)

nwb_declare_static_library(nwb_font_builder_bake)
nwb_target_adhd_sources(nwb_font_builder_bake NAMESPACE "NWB::FontBuilderUtility" SOURCES ${_nwb_font_builder_bake_adhd})
target_link_libraries(nwb_font_builder_bake PUBLIC nwb_utilities_global nwb_assets_font_atlas PRIVATE nwb_assets_font nwb::freetype nwb_common)
unset(_nwb_font_builder_bake_adhd)

nwb_declare_executable(nwb_font_builder)
nwb_target_adhd_sources(nwb_font_builder NAMESPACE "NWB::FontBuilderUtility" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/main.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/command_line.adhd"
)
target_link_libraries(nwb_font_builder PRIVATE nwb_font_builder_bake nwb_common nwb_alloc nwb_logclient nwb::cli11_headers)
