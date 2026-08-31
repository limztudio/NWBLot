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
    "${CMAKE_SOURCE_DIR}/tests/common/vulkan_test_sync.h"
    "${CMAKE_CURRENT_LIST_DIR}/cooperative_vector_query_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/descriptor_heap_bind_ingress_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/descriptor_heap_bind_liveness_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/descriptor_buffer_manager_ingress_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/descriptor_buffer_round_trip_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/extension_command_ingress_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/native_mesh_policy_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ray_tracing_build_ingress_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ray_tracing_dispatch_ingress_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ray_tracing_shader_table_ingress_tests.cpp"
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
    "${CMAKE_CURRENT_LIST_DIR}/command_ir_profile.cpp"
)
target_link_libraries(nwb_command_ir_profile PRIVATE
    nwb_graphics
    nwb_perf
    nwb_alloc
    nwb_common
)

