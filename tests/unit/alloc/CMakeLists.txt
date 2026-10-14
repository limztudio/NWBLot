nwb_declare_gtest_executable(nwb_alloc_tests)
target_sources(nwb_alloc_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/allocator_contention_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/persistent_arena_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/scratch_reuse_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/aligned_reallocation_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/usage_boundary_tests.cpp")
target_link_libraries(nwb_alloc_tests PRIVATE nwb_common nwb_alloc)

target_sources(nwb_alloc_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests/common/arena_owner_fixture.h")
