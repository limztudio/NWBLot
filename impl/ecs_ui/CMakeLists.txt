add_subdirectory(toolkit)

nwb_declare_static_library(nwb_ecs_ui)
target_sources(nwb_ecs_ui PRIVATE
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
)
nwb_target_adhd_sources(nwb_ecs_ui
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
)
target_link_libraries(nwb_ecs_ui PUBLIC nwb_assets nwb_assets_scene nwb_assets_subsystem nwb_ecs nwb_graphics nwb_input nwb_os nwb_ui_edit nwb_ui_gpu nwb_ui_text nwb_ui_widgets)
