nwb_declare_static_library(nwb_ui_images)
target_link_libraries(nwb_ui_images PUBLIC nwb_assets_texture nwb_assets_ui_skin)
nwb_target_adhd_sources(nwb_ui_images
    NAMESPACE "NWB::Impl::Ui"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
)
