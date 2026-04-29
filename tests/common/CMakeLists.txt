nwb_declare_executable(nwb_common_tests)
target_sources(nwb_common_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/common_tests.cpp"
)
target_link_libraries(nwb_common_tests PRIVATE
    nwb_common
    nwb_alloc
)
add_test(NAME nwb_common_tests COMMAND nwb_common_tests)
