nwb_declare_gtest_executable(nwb_mesh_tests)
nwb_target_adhd_sources(nwb_mesh_tests NAMESPACE "NWB::Tests::SkinningCases" SOURCES "${CMAKE_CURRENT_LIST_DIR}/skinning_name_cases.adhd")
target_sources(nwb_mesh_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/skinning_graph_resource_uses_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/skinning_payload_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/tangent_frame_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
)
target_link_libraries(nwb_mesh_tests PRIVATE nwb_ecs_mesh_skinning nwb_ecs_skeleton nwb_ecs_mesh nwb_mesh nwb_common nwb_alloc)
if(NWB_BUILD_PIPELINE)
    nwb_declare_gtest_executable(nwb_mesh_cook_tests)
    nwb_target_adhd_sources(nwb_mesh_cook_tests NAMESPACE "NWB::Tests" SOURCES "${CMAKE_CURRENT_LIST_DIR}/solid_test_types.adhd")
    target_sources(nwb_mesh_cook_tests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/solid_*.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/*.h"
        "${CMAKE_CURRENT_LIST_DIR}/*.hh"
        "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
        "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/*.inl"
        "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
    )
    target_compile_definitions(nwb_mesh_cook_tests PRIVATE NWB_COOK=1)
    target_link_libraries(nwb_mesh_cook_tests PRIVATE nwb_assets_mesh_cook nwb_common nwb_alloc)
endif()

