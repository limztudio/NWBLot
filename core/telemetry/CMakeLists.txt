nwb_declare_static_library(nwb_telemetry_arena_names)
nwb_target_adhd_sources(nwb_telemetry_arena_names NAMESPACE "NWB::Core::Telemetry::TelemetryArenaScope" SOURCES "${CMAKE_CURRENT_LIST_DIR}/arena_names.adhd")

nwb_declare_static_library(nwb_telemetry)
nwb_target_adhd_sources(nwb_telemetry NAMESPACE "NWB::Core::Telemetry" SOURCES "${CMAKE_CURRENT_LIST_DIR}/codec_validation.adhd" "${CMAKE_CURRENT_LIST_DIR}/stream_header.adhd" "${CMAKE_CURRENT_LIST_DIR}/frame_graph_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/frame_graph_wire_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/local_values.adhd")
target_sources(nwb_telemetry PRIVATE
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
)
target_link_libraries(nwb_telemetry PRIVATE nwb_telemetry_arena_names)
target_link_libraries(nwb_telemetry PUBLIC nwb_common nwb_alloc nwb_perf)

