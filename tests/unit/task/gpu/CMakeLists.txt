nwb_declare_gtest_executable(nwb_gpu_task_tests)
nwb_target_adhd_sources(nwb_gpu_task_tests NAMESPACE "NWB::Tests::TaskGraphTestUtils" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/case_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/suite.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_task_analysis_and_placement_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_task_barrier_and_callback_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_task_command_ir_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_task_declaration_and_dependency_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_task_external_and_frontier_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_task_hazard_and_ownership_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_task_packet_and_prelude_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_task_queue_ownership_and_routing_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_task_queue_scoring_and_timing_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_task_resource_set_and_version_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_task_revision_and_submission_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_task_telemetry_and_timing_cases.adhd"
)
target_link_libraries(nwb_gpu_task_tests PRIVATE nwb_gpu_task nwb_common nwb_alloc)
