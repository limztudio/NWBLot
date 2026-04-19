nwb_declare_executable(nwb_scene_tests)
target_sources(nwb_scene_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/scene_tests.cpp"
)
target_link_libraries(nwb_scene_tests PRIVATE
    nwb_scene
    nwb_ecs
    nwb_common
)
add_test(NAME nwb_scene_tests COMMAND nwb_scene_tests)
