nwb_declare_gtest_executable(nwb_ecs_graphics_tests)
nwb_target_adhd_sources(nwb_ecs_graphics_tests NAMESPACE "NWB::Tests::EcsGraphicsCases" SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.adhd")
target_sources(nwb_ecs_graphics_tests PRIVATE hdr10_shader_host.cpp surfel_shader_host.cpp)
target_link_libraries(nwb_ecs_graphics_tests PRIVATE nwb_ecs_mesh_skinning nwb_ecs_skeleton nwb_ecs_scene nwb_ecs_mesh nwb_ecs_render nwb_common nwb_alloc)

set_source_files_properties(
    "${CMAKE_BINARY_DIR}/adhd_gen/nwb_ecs_graphics_tests/tests/unit/ecs_graphics/optical_bounds_cases.adhd.cpp"
    "${CMAKE_BINARY_DIR}/adhd_gen/nwb_ecs_graphics_tests/tests/unit/ecs_graphics/ecs_graphics_mesh_and_optics_cases.adhd.cpp"
    PROPERTIES COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:Clang,AppleClang>:-ffp-model=strict>;$<$<CXX_COMPILER_ID:GNU>:-frounding-math>;$<$<CXX_COMPILER_ID:MSVC>:/fp:strict>"
)
