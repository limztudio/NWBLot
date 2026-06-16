nwb_declare_static_library(nwb_logtelemetry)
target_sources(nwb_logtelemetry PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/ingest.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ingest.h"
    "${CMAKE_CURRENT_LIST_DIR}/report.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/report.h"
)
target_link_libraries(nwb_logtelemetry PUBLIC
    nwb_common
    nwb_alloc
    nwb_telemetry
)
