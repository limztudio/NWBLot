nwb_declare_gtest_executable(nwb_input_tests)
target_sources(nwb_input_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/dispatcher_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/pointer_lifecycle_tests.cpp"
)
target_link_libraries(nwb_input_tests PRIVATE nwb_input)
