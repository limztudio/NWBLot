nwb_declare_gtest_executable(nwb_ecs_graphics_tests)
nwb_target_adhd_sources(nwb_ecs_graphics_tests NAMESPACE "NWB::Tests::EcsGraphicsCases" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/name_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/suite.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_graphics_light_and_material_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_graphics_mesh_and_optics_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_graphics_reflection_and_refraction_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/ecs_graphics_shadow_and_surfel_cases.adhd"
)
target_link_libraries(nwb_ecs_graphics_tests PRIVATE nwb_ecs_mesh_skinning nwb_ecs_skeleton nwb_ecs_scene nwb_ecs_mesh nwb_ecs_render nwb_common nwb_alloc)
