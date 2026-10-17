nwb_declare_gtest_executable(nwb_common_tests)
target_sources(nwb_common_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/name_symbol_decode_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/logging_boundary_tests.cpp"
)
target_link_libraries(nwb_common_tests PRIVATE nwb_common nwb_alloc)

nwb_declare_executable(nwb_common_initializer_tests)
target_sources(nwb_common_initializer_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/initializer_tests.cpp")
target_link_libraries(nwb_common_initializer_tests PRIVATE nwb::gtest nwb_common nwb_alloc)
add_test(NAME nwb_common_initializer_tests COMMAND nwb_common_initializer_tests)

