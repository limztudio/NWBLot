nwb_declare_gtest_executable(nwb_global_tests)
target_sources(nwb_global_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/global_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/job_system_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/sync_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/tangent_frame_tests.inl"
    "${CMAKE_CURRENT_LIST_DIR}/thread_pool_tests.cpp"
)
target_link_libraries(nwb_global_tests PRIVATE
    nwb_common
    nwb_alloc
)

