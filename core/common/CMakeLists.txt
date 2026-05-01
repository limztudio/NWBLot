nwb_declare_static_library(nwb_common)
target_sources(nwb_common PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/common.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/log.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/application_entry.h"
    "${CMAKE_CURRENT_LIST_DIR}/common.h"
    "${CMAKE_CURRENT_LIST_DIR}/log.h"
)
target_link_libraries(nwb_common PUBLIC nwb_alloc nwb::tbb)
