nwb_declare_gtest_executable(nwb_graphics_presentation_tests)
target_sources(nwb_graphics_presentation_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/swapchain_presentation_tests.cpp"
)
target_link_libraries(nwb_graphics_presentation_tests PRIVATE
    nwb_graphics
    nwb_common
    nwb_alloc
)
