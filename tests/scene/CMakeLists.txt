nwb_declare_executable(nwb_scene_tests)
target_sources(nwb_scene_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/scene_tests.cpp"
)
target_link_libraries(nwb_scene_tests PRIVATE
    nwb_scene
    nwb_ecs
    nwb_common
    nwb_alloc
)
add_test(NAME nwb_scene_tests COMMAND nwb_scene_tests)

nwb_declare_executable(nwb_scene_camera_tests)
target_sources(nwb_scene_camera_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/camera_tests.cpp"
)
target_link_libraries(nwb_scene_camera_tests PRIVATE
    nwb_scene
    nwb_common
    nwb_alloc
)
add_test(NAME nwb_scene_camera_tests COMMAND nwb_scene_camera_tests)
