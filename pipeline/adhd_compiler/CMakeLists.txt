nwb_declare_executable(nwb_adhd_compiler)
target_sources(nwb_adhd_compiler PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/compiler_options.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/compiler_options.h"
    "${CMAKE_CURRENT_LIST_DIR}/compiler_driver.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/compiler_driver.h"
)
target_link_libraries(nwb_adhd_compiler PRIVATE
    nwb_metascript
    nwb_common
    nwb_logclient
    nwb::tbb
)
target_compile_definitions(nwb_adhd_compiler PRIVATE __TBB_NO_IMPLICIT_LINKAGE=1 __TBBMALLOC_NO_IMPLICIT_LINKAGE=1)
