nwb_declare_static_library(nwb_input)
target_sources(nwb_input PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
)
target_link_libraries(nwb_input PUBLIC nwb_common nwb_alloc)
