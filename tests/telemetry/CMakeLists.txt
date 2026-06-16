nwb_declare_executable(nwb_telemetry_tests)
target_sources(nwb_telemetry_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/tests.cpp"
)
target_link_libraries(nwb_telemetry_tests PRIVATE
    nwb_telemetry
    nwb_logtelemetry
    nwb_common
    nwb_alloc
)
add_test(NAME nwb_telemetry_tests COMMAND nwb_telemetry_tests)
