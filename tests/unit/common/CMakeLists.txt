nwb_declare_gtest_executable(nwb_common_tests)
nwb_target_adhd_sources(nwb_common_tests NAMESPACE "NWB::Tests::CommonNameCases" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/name_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/suite.adhd"
)
target_link_libraries(nwb_common_tests PRIVATE nwb_common nwb_alloc)

nwb_declare_executable(nwb_common_initializer_tests)
nwb_target_adhd_sources(nwb_common_initializer_tests NAMESPACE "__hidden_common_initializer_tests" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/initializer_case_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/initializer_tests.adhd"
)
target_link_libraries(nwb_common_initializer_tests PRIVATE nwb::gtest nwb_common nwb_alloc nwb_test_common_names)
add_test(NAME nwb_common_initializer_tests COMMAND nwb_common_initializer_tests)
