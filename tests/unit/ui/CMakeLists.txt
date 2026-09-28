nwb_declare_gtest_executable(nwb_ui_tests)
target_sources(nwb_ui_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/paint_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/paint_glyph_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/text_layout_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/text_font_tests.cpp"
)
target_link_libraries(nwb_ui_tests PRIVATE nwb_ui nwb_ui_text)
target_compile_definitions(nwb_ui_tests PRIVATE
    NWB_TEST_FONT_DIRECTORY="${CMAKE_SOURCE_DIR}/impl/assets/ui/fonts/default"
)

