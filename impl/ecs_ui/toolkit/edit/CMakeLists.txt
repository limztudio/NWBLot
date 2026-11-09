nwb_declare_static_library(nwb_ui_edit)
nwb_target_adhd_sources(nwb_ui_edit
    NAMESPACE "NWB::Impl::Ui"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
)
target_link_libraries(nwb_ui_edit PUBLIC nwb_ui)
