nwb_declare_gtest_executable(nwb_ecs_csg_tests)
nwb_target_adhd_sources(nwb_ecs_csg_tests NAMESPACE "NWB::Tests::CsgCases" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/name_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/suite.adhd"
)
target_link_libraries(nwb_ecs_csg_tests PRIVATE nwb_ecs_csg nwb_common nwb_alloc)
