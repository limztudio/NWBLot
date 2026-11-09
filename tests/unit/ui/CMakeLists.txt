nwb_declare_static_library(nwb_ui_name_cases)
nwb_target_adhd_sources(nwb_ui_name_cases NAMESPACE "NWB::Tests::UiCases" SOURCES "${CMAKE_CURRENT_LIST_DIR}/name_cases.adhd")
nwb_target_adhd_sources(nwb_ui_name_cases NAMESPACE "NWB::Tests::UiFixtureText" SOURCES "${CMAKE_CURRENT_LIST_DIR}/fixture_text_shaping.adhd")
nwb_target_adhd_sources(nwb_ui_name_cases NAMESPACE "NWB::UiMultilineTests" SOURCES "${CMAKE_CURRENT_LIST_DIR}/multiline_fixture.adhd")
nwb_target_adhd_sources(nwb_ui_name_cases NAMESPACE "UiTestSupport" SOURCES "${CMAKE_CURRENT_LIST_DIR}/native_input.adhd")
target_link_libraries(nwb_ui_name_cases PUBLIC nwb::gtest nwb_ecs_ui nwb_ui_edit nwb_ui_text nwb_test_common_names)

nwb_declare_gtest_executable(nwb_ui_tests)
nwb_target_adhd_sources(nwb_ui_tests NAMESPACE "__hidden_ui_glyph_sdf_tests" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/glyph_shader_case_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/suite.adhd"
)
target_include_directories(nwb_ui_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(nwb_ui_tests PRIVATE nwb_ui_name_cases nwb_ui_gpu nwb_ui_images nwb_ui_edit nwb_ui_widgets nwb_ui nwb_ui_text nwb_assets_font_atlas nwb::harfbuzz)
target_compile_definitions(nwb_ui_tests PRIVATE NWB_TEST_FONT_DIRECTORY="${CMAKE_SOURCE_DIR}/impl/assets/ui/fonts/default")
