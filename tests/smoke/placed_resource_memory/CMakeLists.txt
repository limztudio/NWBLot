# Standalone validation-backed coverage for placed host-visible virtual buffer memory.
nwb_declare_gtest_executable(nwb_placed_resource_memory_tests)
target_sources(nwb_placed_resource_memory_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/placed_resource_memory_tests.cpp"
)
target_link_libraries(nwb_placed_resource_memory_tests PRIVATE
    nwb_graphics
    nwb_perf
    nwb_alloc
    nwb_common
    nwb_assets_graphics
)
