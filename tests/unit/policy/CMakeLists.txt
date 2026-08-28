add_test(
    NAME nwb_return_value_handling
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/return_value_handling.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_return_value_handling_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/return_value_handling.py"
        --self-test
)
add_test(
    NAME nwb_no_direct_std_in_tests
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_direct_std_in_tests.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_direct_std_in_tests_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_direct_std_in_tests.py"
        --self-test
)
add_test(
    NAME nwb_no_direct_std_in_interop_boundaries
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_direct_std_in_interop_boundaries.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_direct_std_in_interop_boundaries_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_direct_std_in_interop_boundaries.py"
        --self-test
)
add_test(
    NAME nwb_no_core_implementation_dependencies
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_core_implementation_dependencies.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_core_implementation_dependencies_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_core_implementation_dependencies.py"
        --self-test
)
add_test(
    NAME nwb_no_engine_default_bxdf
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_engine_default_bxdf.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_engine_default_bxdf_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_engine_default_bxdf.py"
        --self-test
)
add_test(
    NAME nwb_no_render_lane_facade
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_render_lane_facade.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_render_lane_facade_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_render_lane_facade.py"
        --self-test
)
add_test(
    NAME nwb_no_manual_packet_transaction_facade
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_manual_packet_transaction_facade.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_manual_packet_transaction_facade_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_manual_packet_transaction_facade.py"
        --self-test
)
add_test(
    NAME nwb_no_packet_anchored_submission_bindings
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_packet_anchored_submission_bindings.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_packet_anchored_submission_bindings_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_packet_anchored_submission_bindings.py"
        --self-test
)
add_test(
    NAME nwb_no_recording_time_packet_state_overrides
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_recording_time_packet_state_overrides.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_recording_time_packet_state_overrides_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_recording_time_packet_state_overrides.py"
        --self-test
)
add_test(
    NAME nwb_no_public_single_packet_recording
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_public_single_packet_recording.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_public_single_packet_recording_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_public_single_packet_recording.py"
        --self-test
)
add_test(
    NAME nwb_no_packet_final_state_query
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_packet_final_state_query.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_packet_final_state_query_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_packet_final_state_query.py"
        --self-test
)
add_test(
    NAME nwb_no_public_single_packet_submission
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_public_single_packet_submission.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_public_single_packet_submission_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_public_single_packet_submission.py"
        --self-test
)
add_test(
    NAME nwb_no_vulkan_submission_test_sync
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_vulkan_submission_test_sync.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_vulkan_submission_test_sync_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_vulkan_submission_test_sync.py"
        --self-test
)
add_test(
    NAME nwb_no_staging_texture_test_facade
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_staging_texture_test_facade.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_staging_texture_test_facade_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_staging_texture_test_facade.py"
        --self-test
)
add_test(
    NAME nwb_no_upload_pool_diagnostic_facade
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_upload_pool_diagnostic_facade.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_upload_pool_diagnostic_facade_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_upload_pool_diagnostic_facade.py"
        --self-test
)
add_test(
    NAME nwb_no_grouped_device_desc_queue_fallback
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_grouped_device_desc_queue_fallback.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_grouped_device_desc_queue_fallback_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_grouped_device_desc_queue_fallback.py"
        --self-test
)
add_test(
    NAME nwb_no_hidden_namespaces_in_headers
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_hidden_namespaces_in_headers.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_no_hidden_namespaces_in_headers_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/no_hidden_namespaces_in_headers.py"
        --self-test
)
add_test(
    NAME nwb_source_text_format
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/source_text_format.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_source_text_format_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/source_text_format.py"
        --self-test
)
add_test(
    NAME nwb_test_feature_override_boundary
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/test_feature_override_boundary.py"
        "${CMAKE_SOURCE_DIR}"
)
add_test(
    NAME nwb_test_feature_override_boundary_self_test
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/test_feature_override_boundary.py"
        --self-test
)
