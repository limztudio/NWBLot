nwb_declare_gtest_executable(nwb_ecs_ui_tests)
nwb_target_adhd_sources(nwb_ecs_ui_tests NAMESPACE "NWB::Tests::EcsUi" SOURCES "${CMAKE_CURRENT_LIST_DIR}/suite.adhd" "${CMAKE_CURRENT_LIST_DIR}/ecs_ui_edit_host_and_action_cases.adhd" "${CMAKE_CURRENT_LIST_DIR}/ecs_ui_navigation_and_pointer_cases.adhd" "${CMAKE_CURRENT_LIST_DIR}/ecs_ui_popup_and_selection_cases.adhd")
target_link_libraries(nwb_ecs_ui_tests PRIVATE nwb_ecs_ui nwb_ui_edit nwb_ui_name_cases)
