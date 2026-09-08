nwb_declare_static_library(nwb_task)
target_sources(nwb_task PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task_notify.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cpu_task.h"
    "${CMAKE_CURRENT_LIST_DIR}/arena_names.h"
)
target_link_libraries(nwb_task PUBLIC nwb_alloc)
target_compile_definitions(nwb_task PRIVATE __TBB_NO_IMPLICIT_LINKAGE=1 __TBBMALLOC_NO_IMPLICIT_LINKAGE=1)
