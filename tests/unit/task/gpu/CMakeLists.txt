nwb_declare_gtest_executable(nwb_gpu_task_tests)
nwb_target_adhd_sources(nwb_gpu_task_tests NAMESPACE "NWB::Tests::TaskGraphTestUtils" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/case_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/suite.adhd"
)
target_link_libraries(nwb_gpu_task_tests PRIVATE nwb_gpu_task nwb_common nwb_alloc)
