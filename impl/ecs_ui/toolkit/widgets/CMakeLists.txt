nwb_declare_static_library(nwb_ui_widgets)
target_sources(nwb_ui_widgets PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/*.cxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/*.ixx"
    "${CMAKE_CURRENT_LIST_DIR}/../builder*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../builder*.h"
)
target_link_libraries(nwb_ui_widgets PUBLIC nwb_ui_text nwb_ui_edit)
nwb_target_adhd_sources(nwb_ui_widgets
    NAMESPACE NWB::Impl::Ui
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
)
