nwb_declare_static_library(nwb_ui_gpu)
nwb_target_adhd_sources(nwb_ui_gpu
    NAMESPACE "NWB::Impl::Ui"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
)
target_link_libraries(nwb_ui_gpu PUBLIC nwb_assets_scene nwb_ui nwb_graphics nwb_assets_texture_loader nwb_assets_texture)
target_link_libraries(nwb_ui_gpu PRIVATE nwb_assets_shader_loader)
