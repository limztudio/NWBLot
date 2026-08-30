nwb_declare_gtest_executable(nwb_graphics_presentation_tests)
target_sources(nwb_graphics_presentation_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/compressed_format_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cooperative_vector_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/swapchain_native_identity_contract_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/swapchain_presentation_tests.cpp"
)
target_link_libraries(nwb_graphics_presentation_tests PRIVATE
    nwb_graphics
    nwb_common
    nwb_alloc
)

nwb_declare_gtest_executable(nwb_graphics_task_graph_tests)
target_sources(nwb_graphics_task_graph_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/gpu_crash_marker_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_timing_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_timing_metrics_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/heap_binding_contract_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/host_readback_sync_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/pipeline_cache_validation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/resource_queue_sharing_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/task_graph_resource_version_api_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/task_graph_resource_version_dependency_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/task_graph_resource_version_lifetime_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/task_graph_resource_version_range_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/task_graph_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/task_graph_timing_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/texture_clear_task_graph_tests.cpp"
)
target_link_libraries(nwb_graphics_task_graph_tests PRIVATE
    nwb_graphics
    nwb_common
    nwb_alloc
)

