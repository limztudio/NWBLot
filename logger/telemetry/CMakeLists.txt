nwb_declare_static_library(nwb_logtelemetry)
nwb_target_adhd_sources(nwb_logtelemetry NAMESPACE "NWB::Log" SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.adhd")
target_link_libraries(nwb_logtelemetry PUBLIC nwb_common nwb_alloc nwb_telemetry)
