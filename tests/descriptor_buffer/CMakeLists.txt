# Standalone headless Vulkan round-trip proof for Backend C (VK_EXT_descriptor_buffer).
#
# Brings up a real headless GPU device (no window, no swap chain) and exercises
# DescriptorBufferManager::allocate/writeDescriptor/free across every descriptor class,
# asserting the vkGetDescriptorEXT bytes are produced and the segment sub-allocator is sound.
# Descriptor-buffer-compatible production pipelines consume the manager; this suite also submits a
# minimal production compute pipeline and verifies GPU readback through descriptor-buffer offsets.
#
# GPU-optional: the suite skips cleanly (not fails) when VK_EXT_descriptor_buffer is absent on
# the host (e.g. a non-BC-250 runner), since Backend A remains the portability floor there.
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
)
