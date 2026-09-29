nwb_declare_gtest_executable(nwb_ecs_ui_tests)
target_sources(nwb_ecs_ui_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/text_edit_session_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/text_edit_owned_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/edit_clipboard_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/clipboard_publication_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/edit_box_host_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/popup_input_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/popup_input_fixture.h"
    "${CMAKE_CURRENT_LIST_DIR}/nested_popup_input_tests.cpp"
)
target_link_libraries(nwb_ecs_ui_tests PRIVATE nwb_ecs_ui nwb_ui_edit)
