nwb_declare_executable(nwb_ecs_graphics_tests)
target_sources(nwb_ecs_graphics_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/ecs_graphics_tests.cpp"
)
target_link_libraries(nwb_ecs_graphics_tests PRIVATE
    nwb_ecs_skinned_mesh
    nwb_ecs_skinned_mesh_render
    nwb_ecs_camera
    nwb_ecs_mesh
    nwb_ecs_lighting
    nwb_ecs_scene
    nwb_ecs_render
    nwb_common
    nwb_alloc
)
add_test(NAME nwb_ecs_graphics_tests COMMAND nwb_ecs_graphics_tests)

