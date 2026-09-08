nwb_declare_gtest_executable(nwb_cpu_task_tests)
target_sources(nwb_cpu_task_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_profiling_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_cancellation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_profile_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_range_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_wait_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_scheduler_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_cleanup_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_fanin_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_submission_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_mpmc_tests.cpp"
)
target_link_libraries(nwb_cpu_task_tests PRIVATE
    nwb_cpu_task
    nwb_common
)

set_tests_properties(nwb_cpu_task_tests PROPERTIES TIMEOUT 120)
