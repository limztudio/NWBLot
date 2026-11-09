nwb_declare_static_library(nwb_global_hex_case_types)
nwb_target_adhd_sources(nwb_global_hex_case_types NAMESPACE "__hidden_hex_failure_tests" SOURCES "${CMAKE_CURRENT_LIST_DIR}/hex_case_types.adhd")

nwb_declare_static_library(nwb_global_fixed_vector_case_types)
nwb_target_adhd_sources(nwb_global_fixed_vector_case_types NAMESPACE "__hidden_fixed_vector_failure_tests" SOURCES "${CMAKE_CURRENT_LIST_DIR}/fixed_vector_case_types.adhd")

nwb_declare_static_library(nwb_global_scope_case_types)
nwb_target_adhd_sources(nwb_global_scope_case_types NAMESPACE "__hidden_scope_exit_tests" SOURCES "${CMAKE_CURRENT_LIST_DIR}/scope_case_types.adhd")

nwb_declare_static_library(nwb_global_terminal_case_types)
nwb_target_adhd_sources(nwb_global_terminal_case_types NAMESPACE "__hidden_terminal_entry_tests" SOURCES "${CMAKE_CURRENT_LIST_DIR}/terminal_case_types.adhd")

nwb_declare_gtest_executable(nwb_global_tests)
nwb_target_adhd_sources(nwb_global_tests NAMESPACE "NWB::Tests::GlobalCases" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/name_case_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/algorithm_case_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/global_tests.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/global_arena_and_propagation_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/global_shared_and_failure_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/global_numeric_and_text_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/global_process_and_filesystem_cases.adhd"
)
target_link_libraries(nwb_global_tests PRIVATE nwb_common nwb_alloc nwb_global_hex_case_types nwb_global_fixed_vector_case_types nwb_global_scope_case_types nwb_global_terminal_case_types)

nwb_declare_gtest_executable(nwb_global_contract_boundary_tests)
nwb_target_adhd_sources(nwb_global_contract_boundary_tests NAMESPACE "NWB::Tests::GlobalContractCases" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/contract_name_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/contract_boundary_tests.adhd"
)
target_link_libraries(nwb_global_contract_boundary_tests PRIVATE nwb_common nwb_alloc)
