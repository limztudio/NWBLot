nwb_declare_gtest_executable(nwb_ecs_model_tests)
nwb_target_adhd_sources(nwb_ecs_model_tests NAMESPACE "NWB::Tests::ModelCases" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/name_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/suite.adhd"
)
target_link_libraries(nwb_ecs_model_tests PRIVATE nwb_ecs_model nwb_assets_model nwb_ecs_scene nwb_ecs_mesh nwb_ecs_skeleton nwb_common nwb_alloc)
