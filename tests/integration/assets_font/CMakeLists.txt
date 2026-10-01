nwb_declare_gtest_executable(nwb_assets_font_tests)
target_sources(nwb_assets_font_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/font_tests.cpp")
target_compile_definitions(nwb_assets_font_tests PRIVATE NWB_COOK=1 NWB_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(nwb_assets_font_tests PRIVATE nwb_assets_font_cook nwb_common nwb_alloc nwb::freetype)
