nwb_declare_gtest_executable(nwb_assets_font_atlas_tests)
target_sources(nwb_assets_font_atlas_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/font_atlas_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/font_bundle_tests.cpp"
)
target_compile_definitions(nwb_assets_font_atlas_tests PRIVATE NWB_COOK=1 NWB_REPO_ROOT="${CMAKE_SOURCE_DIR}")
nwb_target_link_libraries_whole_archive(nwb_assets_font_atlas_tests nwb_assets_font_atlas_volume_entry)
target_link_libraries(nwb_assets_font_atlas_tests PRIVATE nwb_assets_font_atlas_cook nwb_assets_cook nwb_common nwb_alloc)
