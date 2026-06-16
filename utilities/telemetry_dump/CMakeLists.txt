nwb_declare_executable(nwb_telemetry_dump)
target_sources(nwb_telemetry_dump PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
)
target_link_libraries(nwb_telemetry_dump PRIVATE
    nwb_common
    nwb_alloc
    nwb_telemetry
    nwb_logtelemetry
)
