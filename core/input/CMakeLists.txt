nwb_declare_static_library(nwb_input)
target_sources(nwb_input PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/input.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/input.h"
)
target_link_libraries(nwb_input PUBLIC nwb_common nwb_alloc)
