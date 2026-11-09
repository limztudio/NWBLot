nwb_declare_gtest_executable(nwb_math_tests)
nwb_target_adhd_sources(nwb_math_tests NAMESPACE "NWB::Tests::MathCases" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/case_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/suite.adhd"
)
target_link_libraries(nwb_math_tests PRIVATE nwb_common nwb_alloc)
