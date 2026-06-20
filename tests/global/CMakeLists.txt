nwb_declare_gtest_executable(nwb_global_tests)
target_sources(nwb_global_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/tests.cpp"
)
target_link_libraries(nwb_global_tests PRIVATE
    nwb_common
    nwb_alloc
)
