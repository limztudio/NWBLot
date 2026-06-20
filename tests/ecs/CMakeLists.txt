nwb_declare_gtest_executable(nwb_ecs_tests)
target_sources(nwb_ecs_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/tests.cpp"
)
target_link_libraries(nwb_ecs_tests PRIVATE
    nwb_ecs
    nwb_common
    nwb_alloc
)
