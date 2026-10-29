nwb_declare_static_library(nwb_alloc)
target_sources(nwb_alloc PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/core.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/persistent.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
)
target_link_libraries(nwb_alloc PUBLIC global_foundation nwb::tbbmalloc)
target_compile_definitions(nwb_alloc PRIVATE __TBB_NO_IMPLICIT_LINKAGE=1 __TBBMALLOC_NO_IMPLICIT_LINKAGE=1)
