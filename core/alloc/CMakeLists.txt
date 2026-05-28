nwb_declare_static_library(nwb_alloc)
target_sources(nwb_alloc PRIVATE
    "${PROJECT_SOURCE_DIR}/global/container/adaptor.cpp"
    "${PROJECT_SOURCE_DIR}/global/container/adaptor.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/core.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/tlsf.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/base.h"
    "${CMAKE_CURRENT_LIST_DIR}/core.h"
    "${CMAKE_CURRENT_LIST_DIR}/general.h"
    "${CMAKE_CURRENT_LIST_DIR}/persistent.h"
    "${CMAKE_CURRENT_LIST_DIR}/scratch.h"
    "${CMAKE_CURRENT_LIST_DIR}/thread.h"
    "${CMAKE_CURRENT_LIST_DIR}/job.h"
)
target_link_libraries(nwb_alloc PUBLIC nwb::tbbmalloc)
target_compile_definitions(nwb_alloc PRIVATE __TBB_NO_IMPLICIT_LINKAGE=1 __TBBMALLOC_NO_IMPLICIT_LINKAGE=1)
