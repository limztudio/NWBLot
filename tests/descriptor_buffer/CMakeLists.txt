# Standalone headless Vulkan round-trip proof for Backend C (VK_EXT_descriptor_buffer).
#
# Brings up a real headless GPU device (no window, no swap chain) and exercises
# DescriptorBufferManager::allocate/writeDescriptor/free across every descriptor class,
# asserting the vkGetDescriptorEXT bytes are produced and the segment sub-allocator is sound.
# This is step 1c: the manager is dark to every live pipeline; this test is its sole consumer
# until the Phase-3 binding-layer conversion lights it up.
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
