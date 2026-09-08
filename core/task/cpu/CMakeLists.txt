nwb_declare_static_library(nwb_cpu_task)
target_sources(nwb_cpu_task PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/scheduler.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/scheduler_notify.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/scheduler_wait.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/scheduler.h"
    "${CMAKE_CURRENT_LIST_DIR}/arena_names.h"
)

target_link_libraries(nwb_cpu_task PUBLIC nwb_alloc)
target_compile_definitions(nwb_cpu_task PRIVATE __TBB_NO_IMPLICIT_LINKAGE=1 __TBBMALLOC_NO_IMPLICIT_LINKAGE=1)
