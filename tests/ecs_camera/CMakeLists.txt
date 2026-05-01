nwb_declare_executable(nwb_ecs_camera_tests)
target_sources(nwb_ecs_camera_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/camera_tests.cpp"
)
target_link_libraries(nwb_ecs_camera_tests PRIVATE
    nwb_ecs_camera
    nwb_common
    nwb_alloc
)
add_test(NAME nwb_ecs_camera_tests COMMAND nwb_ecs_camera_tests)
