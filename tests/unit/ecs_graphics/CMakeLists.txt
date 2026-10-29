nwb_declare_gtest_executable(nwb_ecs_graphics_tests)
target_sources(nwb_ecs_graphics_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/*.cxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/*.ixx"
    "${CMAKE_SOURCE_DIR}/tests/common/meshlet_ref_test_data.h"
)
target_link_libraries(nwb_ecs_graphics_tests PRIVATE nwb_ecs_mesh_skinning nwb_ecs_skeleton nwb_ecs_scene nwb_ecs_mesh nwb_ecs_render nwb_common nwb_alloc)
