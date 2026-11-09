nwb_declare_gtest_executable(nwb_telemetry_tests)
nwb_target_adhd_sources(nwb_telemetry_tests NAMESPACE "NWB::Tests::TelemetryCases" SOURCES "${CMAKE_CURRENT_LIST_DIR}/perf_case_types.adhd")
target_sources(nwb_telemetry_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/*.cxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/*.ixx"
    "${CMAKE_SOURCE_DIR}/tests/common/frame_graph_snapshot_validation.h"
)
target_link_libraries(nwb_telemetry_tests PRIVATE nwb_telemetry nwb_logtelemetry nwb_common nwb_alloc nwb_presentation_smoke_types)
