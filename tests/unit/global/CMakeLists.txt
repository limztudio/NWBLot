nwb_declare_gtest_executable(nwb_global_tests)
target_sources(nwb_global_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/allocator_churn_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/allocator_contention_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/allocator_owner_concurrency_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/allocator_owner_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/persistent_arena_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/arena_object_construction_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/allocator_propagation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/global_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_topology_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_scheduler_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_cleanup_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_fanin_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_submission_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_mpmc_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/scratch_reuse_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/scope_exit_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/sync_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/tangent_frame_tests.inl"
    "${CMAKE_CURRENT_LIST_DIR}/terminal_entry_tests.cpp"
)
target_link_libraries(nwb_global_tests PRIVATE
    nwb_common
    nwb_alloc
)

