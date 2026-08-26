nwb_declare_gtest_executable(nwb_graphics_presentation_tests)
target_sources(nwb_graphics_presentation_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/compressed_format_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/swapchain_presentation_tests.cpp"
)
target_link_libraries(nwb_graphics_presentation_tests PRIVATE
    nwb_graphics
    nwb_common
    nwb_alloc
)

nwb_declare_gtest_executable(nwb_graphics_task_graph_tests)
target_sources(nwb_graphics_task_graph_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/gpu_crash_marker_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gpu_timing_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/heap_binding_contract_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/host_readback_sync_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/task_graph_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/texture_clear_task_graph_tests.cpp"
)
target_link_libraries(nwb_graphics_task_graph_tests PRIVATE
    nwb_graphics
    nwb_common
    nwb_alloc
)
