nwb_declare_gtest_executable(nwb_graphics_presentation_tests)
nwb_target_adhd_sources(nwb_graphics_presentation_tests NAMESPACE "NWB::Tests::GraphicsPresentation" SOURCES "${CMAKE_CURRENT_LIST_DIR}/presentation_suite.adhd")
target_link_libraries(nwb_graphics_presentation_tests PRIVATE nwb_graphics_backend nwb_common nwb_alloc)

if(NWB_GRAPHICS_BACKEND STREQUAL "Vulkan")
    set(nwb_vulkan_resource_tests "${CMAKE_CURRENT_LIST_DIR}/vulkan_cases.adhd")
endif()
nwb_declare_gtest_executable(nwb_graphics_resource_tests)
nwb_target_adhd_sources(nwb_graphics_resource_tests NAMESPACE "NWB::Tests::GraphicsTimingCases" SOURCES
    ${nwb_vulkan_resource_tests}
    "${CMAKE_CURRENT_LIST_DIR}/gpu_timing_test_names.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/resource_*.adhd"
)
target_link_libraries(nwb_graphics_resource_tests PRIVATE nwb_graphics nwb_graphics_backend nwb_common nwb_alloc)
