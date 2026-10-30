nwb_declare_static_library(nwb_pipeline_adhd_compiler)
target_sources(nwb_pipeline_adhd_compiler PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/compile.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
)
target_link_libraries(nwb_pipeline_adhd_compiler PUBLIC nwb_metascript nwb_common)

nwb_declare_executable(nwb_adhd_compiler)
target_sources(nwb_adhd_compiler PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
)
target_link_libraries(nwb_adhd_compiler PRIVATE nwb_pipeline_command_line nwb_pipeline_adhd_compiler nwb_logclient nwb::tbb)
target_compile_definitions(nwb_adhd_compiler PRIVATE NWB_PIPELINE_TOOL_NAME="adhd_compiler" __TBB_NO_IMPLICIT_LINKAGE=1 __TBBMALLOC_NO_IMPLICIT_LINKAGE=1)

