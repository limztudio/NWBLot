nwb_declare_gtest_executable(nwb_filesystem_tests)
nwb_target_adhd_sources(nwb_filesystem_tests NAMESPACE "NWB::Tests::FilesystemCases" SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.adhd")
target_link_libraries(nwb_filesystem_tests PRIVATE nwb_filesystem)
