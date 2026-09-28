nwb_declare_gtest_executable(nwb_assets_ui_skin_tests)
target_sources(nwb_assets_ui_skin_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/ui_skin_tests.cpp"
)
target_compile_definitions(nwb_assets_ui_skin_tests PRIVATE
    NWB_COOK=1
    NWB_REPO_ROOT="${CMAKE_SOURCE_DIR}"
)
nwb_target_link_libraries_whole_archive(nwb_assets_ui_skin_tests
    nwb_assets_ui_skin_volume_entry
)
target_link_libraries(nwb_assets_ui_skin_tests PRIVATE
    nwb_assets_ui_skin_cook
    nwb_assets_texture_cook
    nwb_assets_cook
    nwb_common
    nwb_alloc
)

