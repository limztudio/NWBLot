nwb_declare_executable(nwb_ecs_graphics_tests)
target_sources(nwb_ecs_graphics_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/ecs_graphics_tests.cpp"
)
target_link_libraries(nwb_ecs_graphics_tests PRIVATE
    nwb_ecs_graphics
    nwb_scene
    nwb_common
)
add_test(NAME nwb_ecs_graphics_tests COMMAND nwb_ecs_graphics_tests)
