nwb_declare_gtest_executable(nwb_global_tests)
target_sources(nwb_global_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/base64_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/atomic_identity_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/binary_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/collection_algorithm_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/diagnostic_event_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/allocator_owner_concurrency_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/allocator_owner_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/arena_object_construction_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/allocator_propagation_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/global_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/global_binary_shared_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/global_numeric_shared_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/global_collection_shared_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/global_hash_shared_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/noexcept_failure_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/pointer_failure_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/hex_failure_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/fixed_vector_failure_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/numeric_boundary_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_topology_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/scope_exit_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/sync_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/terminal_entry_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/text_boundary_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/text_utility_edge_tests.cpp"
)
target_sources(nwb_global_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/simd_memory_tests.cpp"
    "${CMAKE_SOURCE_DIR}/tests/common/simd_memory_guard_fixture.cpp"
    "${CMAKE_SOURCE_DIR}/tests/common/simd_memory_guard_fixture.h"
)
target_sources(nwb_global_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/name_identity_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/process_filesystem_boundary_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/process_execution_boundary_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/shared_library_boundary_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/process_memory_map_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/filesystem_cleanup_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/filesystem_staging_tests.cpp"
)
target_link_libraries(nwb_global_tests PRIVATE nwb_common nwb_alloc)


nwb_declare_gtest_executable(nwb_global_contract_boundary_tests)
target_sources(nwb_global_contract_boundary_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/contract_boundary_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/pointer_contract_boundary_tests.cpp"
)
target_link_libraries(nwb_global_contract_boundary_tests PRIVATE nwb_common nwb_alloc)

target_sources(nwb_global_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/tests/common/arena_owner_fixture.h"
)

