nwb_declare_gtest_executable(nwb_graphics_presentation_tests)
nwb_target_adhd_sources(nwb_graphics_presentation_tests NAMESPACE "NWB::Tests::GraphicsPresentation" SOURCES "${CMAKE_CURRENT_LIST_DIR}/presentation_suite.adhd")
target_link_libraries(nwb_graphics_presentation_tests PRIVATE nwb_graphics_backend nwb_common nwb_alloc)

nwb_declare_gtest_executable(nwb_graphics_resource_tests)
nwb_target_adhd_sources(nwb_graphics_resource_tests NAMESPACE "NWB::Tests::GraphicsTimingCases" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/gpu_timing_test_names.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/resource_suite.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/resource_timing_and_queue_cases.adhd"
)
target_link_libraries(nwb_graphics_resource_tests PRIVATE nwb_graphics nwb_graphics_backend nwb_common nwb_alloc)
