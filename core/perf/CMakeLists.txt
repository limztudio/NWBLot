nwb_declare_static_library(nwb_perf)
target_sources(nwb_perf PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/memory.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/session.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/timing.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/memory.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/report.h"
    "${CMAKE_CURRENT_LIST_DIR}/scope_registry.h"
    "${CMAKE_CURRENT_LIST_DIR}/session.h"
    "${CMAKE_CURRENT_LIST_DIR}/timing.h"
)
target_link_libraries(nwb_perf PUBLIC nwb_alloc)

