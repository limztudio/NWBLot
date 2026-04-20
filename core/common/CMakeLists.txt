nwb_declare_static_library(nwb_common)
target_sources(nwb_common PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/common.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/common.h"
)
target_link_libraries(nwb_common PUBLIC nwb::tbb)
