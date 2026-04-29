nwb_declare_executable(nwb_global_tests)
target_sources(nwb_global_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/global_tests.cpp"
)
target_link_libraries(nwb_global_tests PRIVATE
    nwb_common
    nwb_alloc
)
add_test(NAME nwb_global_tests COMMAND nwb_global_tests)
