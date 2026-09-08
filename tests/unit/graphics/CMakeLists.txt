nwb_declare_gtest_executable(nwb_graphics_presentation_tests)
target_sources(nwb_graphics_presentation_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/compressed_format_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cooperative_vector_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/device_creation_policy_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/native_object_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/swapchain_native_identity_contract_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/swapchain_presentation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/vulkan_adapter_capability_tests.cpp"
)
target_link_libraries(nwb_graphics_presentation_tests PRIVATE
    nwb_graphics_backend
    nwb_common
    nwb_alloc
)

nwb_declare_gtest_executable(nwb_graphics_resource_tests)
target_sources(nwb_graphics_resource_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/buffer_range_handoff_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/buffer_range_state_tracking_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/command_buffer_resource_references_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_crash_marker_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_timing_metric_correlator_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_timing_metrics_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_timing_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/heap_binding_contract_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/host_readback_sync_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/pipeline_cache_validation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/resource_queue_sharing_tests.cpp"
)
target_link_libraries(nwb_graphics_resource_tests PRIVATE
    nwb_graphics_backend
    nwb_common
    nwb_alloc
)
