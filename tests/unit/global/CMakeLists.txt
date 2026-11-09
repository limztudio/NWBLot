nwb_declare_static_library(nwb_global_hex_case_types)
nwb_target_adhd_sources(nwb_global_hex_case_types NAMESPACE "__hidden_hex_failure_tests" SOURCES "${CMAKE_CURRENT_LIST_DIR}/hex_case_types.adhd")

nwb_declare_static_library(nwb_global_fixed_vector_case_types)
nwb_target_adhd_sources(nwb_global_fixed_vector_case_types NAMESPACE "__hidden_fixed_vector_failure_tests" SOURCES "${CMAKE_CURRENT_LIST_DIR}/fixed_vector_case_types.adhd")

nwb_declare_static_library(nwb_global_scope_case_types)
nwb_target_adhd_sources(nwb_global_scope_case_types NAMESPACE "__hidden_scope_exit_tests" SOURCES "${CMAKE_CURRENT_LIST_DIR}/scope_case_types.adhd")

nwb_declare_static_library(nwb_global_terminal_case_types)
nwb_target_adhd_sources(nwb_global_terminal_case_types NAMESPACE "__hidden_terminal_entry_tests" SOURCES "${CMAKE_CURRENT_LIST_DIR}/terminal_case_types.adhd")

nwb_declare_gtest_executable(nwb_global_tests)
nwb_target_adhd_sources(nwb_global_tests NAMESPACE "NWB::Tests::GlobalCases" SOURCES "${CMAKE_CURRENT_LIST_DIR}/name_case_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/algorithm_case_types.adhd")
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
target_link_libraries(nwb_global_tests PRIVATE nwb_common nwb_alloc nwb_global_hex_case_types nwb_global_fixed_vector_case_types nwb_global_scope_case_types nwb_global_terminal_case_types)


nwb_declare_gtest_executable(nwb_global_contract_boundary_tests)
nwb_target_adhd_sources(nwb_global_contract_boundary_tests NAMESPACE "NWB::Tests::GlobalContractCases" SOURCES "${CMAKE_CURRENT_LIST_DIR}/contract_name_cases.adhd")
target_sources(nwb_global_contract_boundary_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/contract_boundary_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/pointer_contract_boundary_tests.cpp"
)
target_link_libraries(nwb_global_contract_boundary_tests PRIVATE nwb_common nwb_alloc)

target_sources(nwb_global_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/tests/common/arena_owner_fixture.h"
)

