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
    "${CMAKE_CURRENT_LIST_DIR}/global_tests.cpp"
    "${CMAKE_SOURCE_DIR}/tests/common/simd_memory_guard_fixture.h"
    "${CMAKE_SOURCE_DIR}/tests/common/arena_owner_fixture.h"
)
target_link_libraries(nwb_global_tests PRIVATE nwb_common nwb_alloc nwb_global_hex_case_types nwb_global_fixed_vector_case_types nwb_global_scope_case_types nwb_global_terminal_case_types)

nwb_declare_gtest_executable(nwb_global_contract_boundary_tests)
nwb_target_adhd_sources(nwb_global_contract_boundary_tests NAMESPACE "NWB::Tests::GlobalContractCases" SOURCES "${CMAKE_CURRENT_LIST_DIR}/contract_name_cases.adhd")
target_sources(nwb_global_contract_boundary_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/contract_boundary_tests.cpp"
)
target_link_libraries(nwb_global_contract_boundary_tests PRIVATE nwb_common nwb_alloc)

