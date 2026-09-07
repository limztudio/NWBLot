# Standalone headless Vulkan round-trip proof for the required VK_EXT_descriptor_buffer path.
#
# Brings up a real headless GPU device (no window, no swap chain) and exercises
# DescriptorBufferManager::allocate/writeDescriptor/free across the covered descriptor classes,
# asserting the vkGetDescriptorEXT bytes are produced and the segment sub-allocator is sound.
# Descriptor-buffer-compatible production pipelines consume the manager; this suite also submits a
# minimal production compute pipeline and verifies GPU readback through descriptor-buffer offsets.
#
# GPU-optional: the suite skips cleanly (not fails) when the host cannot run VK_EXT_descriptor_buffer. This is
# descriptor-buffer-only contract coverage; a skipped run is an environment limitation, not a renderer queue-routing test.
nwb_declare_gtest_executable(nwb_descriptor_buffer_tests)
target_sources(nwb_descriptor_buffer_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/tests/common/gpu_task_graph_read_views.h"
    "${CMAKE_SOURCE_DIR}/tests/common/vulkan_test_sync.h"
    "${CMAKE_CURRENT_LIST_DIR}/cooperative_vector_query_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/descriptor_heap_bind_ingress_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/descriptor_heap_bind_liveness_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/descriptor_buffer_manager_ingress_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/acceptance_observers_test_support.h"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/async_avboit_accumulation_lifecycle_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/async_avboit_extinction_lifecycle_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/async_avboit_occupancy_lifecycle_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/async_compute_failure_recovery_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/avboit_accumulation_alias_free_emulation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/avboit_accumulation_csg_emulation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/avboit_accumulation_pair_emulation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/avboit_accumulation_quadruple_emulation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/avboit_accumulation_triple_emulation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/avboit_extinction_alias_free_emulation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/avboit_extinction_csg_emulation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/avboit_extinction_pair_emulation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/avboit_extinction_quadruple_emulation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/avboit_extinction_triple_emulation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/avboit_integration_tail_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/avboit_occupancy_alias_free_emulation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/avboit_occupancy_csg_emulation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/avboit_occupancy_pair_emulation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/avboit_occupancy_quadruple_emulation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/avboit_occupancy_triple_emulation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/avboit_occupancy_vertex_handoff_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/avboit_probes_test_support.h"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/caustic_probes_test_support.h"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/command_buffer_resource_lifetime_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/command_ir_replay_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/command_list_semantic_rejection_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/command_list_submission_preflight_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/command_list_worker_recording_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/csg_probes_test_support.h"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/dedicated_compute_lighting_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/dedicated_compute_ownership_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/dedicated_compute_texture_recovery_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/descriptor_buffer_allocation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/descriptor_buffer_layout_validation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/descriptor_heap_dispatch_retirement_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/descriptor_heap_recording_lease_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/descriptor_heap_write_validation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/device_generation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/device_recreation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/external_final_state_fan_in_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/external_final_state_import_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/external_state_test_support.h"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/frame_lifecycle_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/frame_timing_packets_test_support.h"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/global_descriptor_heap_layout_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/gpu_timing_completion_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/gpu_timing_feedback_demand_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/gpu_timing_feedback_retirement_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/gpu_timing_frame_transaction_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/gpu_timing_measure_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/gpu_timing_stale_scope_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/gpu_timing_submission_ticket_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_acceleration_structure_state_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_acceptance_publication_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_automatic_timing_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_auxiliary_queue_routing_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_avboit_material_state_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_avboit_vertex_handoff_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_builtin_buffer_upload_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_builtin_clear_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_builtin_texture_copy_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_builtin_transfer_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_caustic_accumulator_state_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_caustic_resolve_state_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_concurrent_submission_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_csg_attachment_state_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_csg_emulation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_declaration_mutation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_discard_exception_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_executor_frontier_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_executor_recovery_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_frame_recovery_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_frontier_exception_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_material_entry_state_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_recording_capability_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_recording_lease_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_recording_retry_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_resources_test_support.h"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_shadow_material_restore_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_shadow_visibility_state_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_shared_opaque_emulation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_skinning_deformation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_skinning_state_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_skinning_test_support.h"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_software_bvh_state_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_software_caustic_state_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_software_traversal_state_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_submission_reader_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_surfel_state_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_texture_upload_completion_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_timing_recovery_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_timing_rejection_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_transaction_ownership_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_transparent_trace_state_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_upload_queue_routing_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_upload_recovery_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graph_upload_validation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/graphics_semantic_rejection_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/imgui_texture_layout_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/imported_final_state_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/imported_queue_ownership_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/material_probes_test_support.h"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/native_packet_prefix_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/native_packet_shared_transaction_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/native_packet_traversal_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/native_upload_reuse_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/packet_capability_preflight_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/packet_recording_test_support.h"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/packet_retry_test_support.h"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/parallel_packet_ownership_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/parallel_packet_recording_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/parallel_recording_test_support.h"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/permanent_buffer_state_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/permanent_state_acceptance_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/persistent_state_cache_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/physical_queue_submission_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/placed_resource_binding_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/presentation_submission_lifecycle_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/queue_submission_recovery_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/ray_geometry_pipeline_layouts_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/ray_tracing_descriptor_layout_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/recording_capability_test_support.h"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/renderer_pipeline_layouts_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/renderer_state_chain_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/renderer_submission_rejection_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/round_trip_fixture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/round_trip_fixture.h"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/shaders_test_support.h"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/state_handoff_validation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/submission_callbacks_test_support.h"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/submission_signals_test_support.h"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/surfel_pipeline_layouts_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/texture_clear_render_pass_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/texture_copy_preflight_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/texture_format_preflight_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/timer_query_queue_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/timing_capture_test_support.h"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/timing_preamble_test_support.h"
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/timing_scopes_test_support.h"
    "${CMAKE_CURRENT_LIST_DIR}/extension_command_ingress_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/native_mesh_policy_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ray_tracing_build_ingress_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ray_tracing_dispatch_ingress_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ray_tracing_shader_table_ingress_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/task_graph_runtime_ownership_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/timer_query_lifecycle_tests.cpp"
)
target_link_libraries(nwb_descriptor_buffer_tests PRIVATE
    nwb_frame
    nwb_graphics
    nwb_perf
    nwb_alloc
    nwb_common
    nwb_assets_graphics
    nwb_ecs_ui
)

# Manual target-hardware A/B probe for Phase 10 transfer-upload profiling.  It deliberately is not a CTest:
# a useful result needs a genuinely dedicated Transfer family and an external bandwidth capture on the target GPU.
nwb_declare_executable(nwb_transfer_upload_profile)
target_sources(nwb_transfer_upload_profile PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/transfer_upload_profile.cpp"
)
target_link_libraries(nwb_transfer_upload_profile PRIVATE
    nwb_graphics
    nwb_perf
    nwb_alloc
    nwb_common
)

# Phase 11 optional-command-IR CPU profiling. This remains a manually launched target so capture/decode/replay
# overhead is reported separately from the native renderer path.
nwb_declare_executable(nwb_command_ir_profile)
target_sources(nwb_command_ir_profile PRIVATE
    "${CMAKE_SOURCE_DIR}/tests/common/gpu_task_graph_read_views.h"
    "${CMAKE_CURRENT_LIST_DIR}/command_ir_profile.cpp"
)
target_link_libraries(nwb_command_ir_profile PRIVATE
    nwb_graphics
    nwb_perf
    nwb_alloc
    nwb_common
)

