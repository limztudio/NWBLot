nwb_declare_gtest_executable(nwb_physics_tests)
nwb_target_adhd_sources(nwb_physics_tests NAMESPACE "NWB::Tests::PhysicsCases" SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.adhd")
target_link_libraries(nwb_physics_tests PRIVATE nwb_physics nwb_physics_gpu nwb_ecs_physics nwb_common nwb_alloc nwb_cpu_task nwb_ecs)
