nwb_declare_gtest_executable(nwb_telemetry_tests)
target_sources(nwb_telemetry_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/allocation_owner_telemetry_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/capture_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame_graph_codec_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame_graph_packet_submission_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame_graph_physical_queue_statistics_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame_graph_registry_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame_graph_runtime_statistics_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame_graph_statistics_scaling_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame_graph_test_helpers.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame_graph_test_helpers.h"
    "${CMAKE_CURRENT_LIST_DIR}/frame_graph_wire_test_helpers.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame_graph_wire_test_helpers.h"
    "${CMAKE_CURRENT_LIST_DIR}/perf_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/recorder_codec_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/report_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/telemetry_test_helpers.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/telemetry_test_helpers.h"
)
target_link_libraries(nwb_telemetry_tests PRIVATE
    nwb_telemetry
    nwb_logtelemetry
    nwb_common
    nwb_alloc
)
