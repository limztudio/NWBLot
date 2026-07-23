nwb_declare_static_library(nwb_global)
target_sources(nwb_global PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/allocation_size.h"
    "${CMAKE_CURRENT_LIST_DIR}/container/adaptor.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/container/adaptor.h"
    "${CMAKE_CURRENT_LIST_DIR}/inplace_function.h"
)
