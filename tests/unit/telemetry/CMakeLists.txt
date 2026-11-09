nwb_declare_gtest_executable(nwb_telemetry_tests)
nwb_target_adhd_sources(nwb_telemetry_tests NAMESPACE "NWB::Tests::TelemetryCases" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/perf_case_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/suite.adhd"
)
target_link_libraries(nwb_telemetry_tests PRIVATE nwb_telemetry nwb_logtelemetry nwb_common nwb_alloc nwb_presentation_smoke_types)
