nwb_declare_static_library(nwb_ecs_publication_case_types)
nwb_target_adhd_sources(nwb_ecs_publication_case_types NAMESPACE "__hidden_publication_failure_tests" SOURCES "${CMAKE_CURRENT_LIST_DIR}/publication_case_types.adhd")

nwb_declare_gtest_executable(nwb_ecs_tests)
nwb_target_adhd_sources(nwb_ecs_tests NAMESPACE "__hidden_ecs_tests" SOURCES "${CMAKE_CURRENT_LIST_DIR}/basic_case_types.adhd")
target_sources(nwb_ecs_tests PRIVATE
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
)
target_link_libraries(nwb_ecs_tests PRIVATE nwb_ecs nwb_common nwb_alloc nwb_ecs_publication_case_types)
