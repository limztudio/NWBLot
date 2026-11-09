nwb_declare_static_library(nwb_ecs_publication_case_types)
nwb_target_adhd_sources(nwb_ecs_publication_case_types NAMESPACE "__hidden_publication_failure_tests" SOURCES "${CMAKE_CURRENT_LIST_DIR}/publication_case_types.adhd")

nwb_declare_gtest_executable(nwb_ecs_tests)
nwb_target_adhd_sources(nwb_ecs_tests NAMESPACE "__hidden_ecs_tests" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/basic_case_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/suite.adhd"
)
target_link_libraries(nwb_ecs_tests PRIVATE nwb_ecs nwb_common nwb_alloc nwb_ecs_publication_case_types)
