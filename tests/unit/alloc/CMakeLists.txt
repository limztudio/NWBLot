nwb_declare_gtest_executable(nwb_alloc_tests)
nwb_target_adhd_sources(nwb_alloc_tests NAMESPACE "NWB::Tests::AllocCases" SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.adhd")
target_link_libraries(nwb_alloc_tests PRIVATE nwb_common nwb_alloc)
