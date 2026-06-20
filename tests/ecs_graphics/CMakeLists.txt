nwb_declare_gtest_executable(nwb_ecs_graphics_tests)
target_sources(nwb_ecs_graphics_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../meshlet_ref_test_data.h"
)
target_link_libraries(nwb_ecs_graphics_tests PRIVATE
    nwb_ecs_mesh_skinning
    nwb_ecs_skeleton
    nwb_ecs_scene
    nwb_ecs_mesh
    nwb_ecs_render
    nwb_common
    nwb_alloc
)

