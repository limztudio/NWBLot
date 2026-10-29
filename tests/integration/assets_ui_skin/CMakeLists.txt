nwb_declare_gtest_executable(nwb_assets_ui_skin_tests)
target_sources(nwb_assets_ui_skin_tests PRIVATE
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
target_compile_definitions(nwb_assets_ui_skin_tests PRIVATE NWB_COOK=1 NWB_REPO_ROOT="${CMAKE_SOURCE_DIR}")
nwb_target_link_libraries_whole_archive(nwb_assets_ui_skin_tests nwb_assets_ui_skin_volume_entry)
target_link_libraries(nwb_assets_ui_skin_tests PRIVATE nwb_assets_ui_skin_cook nwb_assets_texture_cook nwb_assets_cook nwb_common nwb_alloc)
