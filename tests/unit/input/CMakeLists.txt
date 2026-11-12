nwb_declare_gtest_executable(nwb_input_tests)
nwb_target_adhd_sources(nwb_input_tests NAMESPACE "NWB::Tests::Input" SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.adhd")
target_link_libraries(nwb_input_tests PRIVATE nwb_input)
