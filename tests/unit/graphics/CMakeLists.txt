nwb_declare_gtest_executable(nwb_graphics_presentation_tests)
target_sources(nwb_graphics_presentation_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/cooperative_vector_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/device_creation_policy_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/presentation_receipt_tests.cpp"
)
target_link_libraries(nwb_graphics_presentation_tests PRIVATE
    nwb_graphics_backend
    nwb_common
    nwb_alloc
)

nwb_declare_gtest_executable(nwb_graphics_resource_tests)
target_sources(nwb_graphics_resource_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/gpu_crash_marker_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_timing_metric_correlator_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_timing_metrics_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_timing_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/resource_queue_sharing_tests.cpp"
)
target_link_libraries(nwb_graphics_resource_tests PRIVATE
    nwb_graphics_backend
    nwb_common
    nwb_alloc
)
