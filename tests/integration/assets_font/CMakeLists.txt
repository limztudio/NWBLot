nwb_declare_gtest_executable(nwb_assets_font_tests)
nwb_target_adhd_sources(nwb_assets_font_tests NAMESPACE "NWB::Tests::FontCases" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/name_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/suite.adhd"
)
target_compile_definitions(nwb_assets_font_tests PRIVATE NWB_COOK=1 NWB_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(nwb_assets_font_tests PRIVATE nwb_assets_font_cook nwb_common nwb_alloc nwb::freetype)
