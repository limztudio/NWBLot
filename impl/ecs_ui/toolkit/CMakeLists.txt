nwb_declare_static_library(nwb_ui)
nwb_target_adhd_sources(nwb_ui
    NAMESPACE "NWB::Impl::Ui"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/context.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/global.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/id.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/paint.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/paint_image_bindings.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/paint_primitives.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/paint_types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/state/*.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/layout/*.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/input/*.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/text/atlas_types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/text/layout_clusters.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/text/layout_measurement.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/text/layout_types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/text/sdf_support.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/text/shaper_glyph_types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/text/shaper_types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/text/types.adhd"
)
target_link_libraries(nwb_ui PUBLIC nwb_input nwb_assets_ui_skin nwb_assets_font nwb_assets_font_atlas)

add_subdirectory(edit)
add_subdirectory(text)
add_subdirectory(images)
target_link_libraries(nwb_ui PUBLIC nwb_ui_images)
add_subdirectory(widgets)
add_subdirectory(gpu)
