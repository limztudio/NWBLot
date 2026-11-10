nwb_declare_gtest_executable(nwb_ecs_ui_tests)
nwb_target_adhd_sources(nwb_ecs_ui_tests NAMESPACE "NWB::Tests::EcsUi" SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.adhd")
target_link_libraries(nwb_ecs_ui_tests PRIVATE nwb_ecs_ui nwb_ui_edit nwb_ui_name_cases)
