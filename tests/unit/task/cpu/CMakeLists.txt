nwb_declare_gtest_executable(nwb_task_tests)
target_sources(nwb_task_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_scheduler_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_cleanup_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_fanin_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_submission_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_mpmc_tests.cpp"
)
target_link_libraries(nwb_task_tests PRIVATE
    nwb_task
    nwb_common
)
