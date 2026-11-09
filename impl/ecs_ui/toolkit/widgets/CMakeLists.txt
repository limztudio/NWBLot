nwb_declare_static_library(nwb_ui_widgets)
target_link_libraries(nwb_ui_widgets PUBLIC nwb_ui_text nwb_ui_edit)
nwb_target_adhd_sources(nwb_ui_widgets
    NAMESPACE "NWB::Impl::Ui"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/../builder.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/../builder_scope.adhd"
)
