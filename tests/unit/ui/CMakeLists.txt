nwb_declare_gtest_executable(nwb_ui_tests)
target_sources(nwb_ui_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/paint_tests.cpp"
)
target_link_libraries(nwb_ui_tests PRIVATE nwb_ui)

