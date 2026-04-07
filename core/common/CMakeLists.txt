nwb_declare_static_library(common)
target_sources(common PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/common.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/common.h"
)
target_link_libraries(common PUBLIC nwb::curl nwb::tbb)
