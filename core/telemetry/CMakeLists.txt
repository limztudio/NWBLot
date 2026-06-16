nwb_declare_static_library(nwb_telemetry)
target_sources(nwb_telemetry PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/recorder.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/event.h"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/recorder.h"
)
target_link_libraries(nwb_telemetry PUBLIC nwb_alloc)

