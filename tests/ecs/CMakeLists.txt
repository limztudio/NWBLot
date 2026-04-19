nwb_declare_executable(nwb_ecs_tests)
target_sources(nwb_ecs_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/ecs_tests.cpp"
)
target_link_libraries(nwb_ecs_tests PRIVATE
    nwb_ecs
    nwb_common
    nwb_alloc
)
add_test(NAME nwb_ecs_tests COMMAND nwb_ecs_tests)
