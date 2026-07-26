# Standalone headless Vulkan round-trip proof for the required VK_EXT_descriptor_buffer path.
#
# Brings up a real headless GPU device (no window, no swap chain) and exercises
# DescriptorBufferManager::allocate/writeDescriptor/free across the covered descriptor classes,
# asserting the vkGetDescriptorEXT bytes are produced and the segment sub-allocator is sound.
# Descriptor-buffer-compatible production pipelines consume the manager; this suite also submits a
# minimal production compute pipeline and verifies GPU readback through descriptor-buffer offsets.
#
# GPU-optional: the suite skips cleanly (not fails) when the host cannot run VK_EXT_descriptor_buffer. This is
# descriptor-buffer-only contract coverage; a skipped run is an environment limitation, not renderer fallback.
nwb_declare_gtest_executable(nwb_descriptor_buffer_tests)
target_sources(nwb_descriptor_buffer_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/descriptor_buffer_round_trip_tests.cpp"
)
target_link_libraries(nwb_descriptor_buffer_tests PRIVATE
    nwb_frame
    nwb_graphics
    nwb_perf
    nwb_alloc
    nwb_common
    nwb_assets_graphics
)
