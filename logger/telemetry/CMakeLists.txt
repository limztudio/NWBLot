nwb_declare_static_library(nwb_logtelemetry)
target_sources(nwb_logtelemetry PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/ingest.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ingest.h"
    "${CMAKE_CURRENT_LIST_DIR}/memory_report.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/memory_report.h"
    "${CMAKE_CURRENT_LIST_DIR}/report.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/report.h"
    "${CMAKE_CURRENT_LIST_DIR}/report_dot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/report_dot.h"
)
target_link_libraries(nwb_logtelemetry PUBLIC
    nwb_common
    nwb_alloc
    nwb_telemetry
)
