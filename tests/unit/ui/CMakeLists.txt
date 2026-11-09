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
    "${CMAKE_CURRENT_LIST_DIR}/ui_core_and_list_fixtures.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_numeric_and_control_fixtures.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_builder_and_combo_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_combo_loan_and_command_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_context_and_control_input_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_control_and_edit_caret_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_edit_model_and_selection_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_horizontal_nav_and_image_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_input_and_list_behavior_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_list_and_multiline_model_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_multiline_and_nested_popup_context_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_nested_popup_router_and_trigger_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_numeric_edit_and_glyph_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_paint_and_overlay_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_pointer_gesture_and_sources_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_popup_builder_and_layout_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_popup_router_and_tools_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_progress_and_radio_builder_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_radio_and_scroll_layout_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_search_combo_and_separator_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_slider_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_text_area_input_and_reentry_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_text_area_scroll_and_baked_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_text_font_and_layout_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_text_region_and_visible_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_texture_and_tooltip_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ui_wheel_and_window_cases.adhd"
)
target_include_directories(nwb_ui_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(nwb_ui_tests PRIVATE nwb_ui_name_cases nwb_ui_gpu nwb_ui_images nwb_ui_edit nwb_ui_widgets nwb_ui nwb_ui_text nwb_assets_font_atlas nwb::harfbuzz)
target_compile_definitions(nwb_ui_tests PRIVATE NWB_TEST_FONT_DIRECTORY="${CMAKE_SOURCE_DIR}/impl/assets/ui/fonts/default")
