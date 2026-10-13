nwb_declare_gtest_executable(nwb_global_tests)
target_sources(nwb_global_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/base64_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/allocator_contention_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/allocator_owner_concurrency_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/allocator_owner_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/persistent_arena_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/arena_object_construction_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/allocator_propagation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/global_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/noexcept_failure_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/name_symbol_decode_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/cpu_topology_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/scratch_reuse_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/scope_exit_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/sync_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/terminal_entry_tests.cpp")
target_sources(nwb_global_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/simd_memory_tests.cpp")
target_sources(nwb_global_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/name_identity_tests.cpp" "${CMAKE_CURRENT_LIST_DIR}/process_filesystem_boundary_tests.cpp")
target_link_libraries(nwb_global_tests PRIVATE nwb_common nwb_alloc)


nwb_declare_gtest_executable(nwb_global_contract_boundary_tests)
target_sources(nwb_global_contract_boundary_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/contract_boundary_tests.cpp")
target_link_libraries(nwb_global_contract_boundary_tests PRIVATE nwb_common nwb_alloc)
