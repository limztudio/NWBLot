nwb_declare_static_library(nwb_common)
target_sources(nwb_common PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/*.cxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/*.ixx"
)
target_link_libraries(nwb_common PUBLIC nwb_alloc nwb::tbb)


nwb_declare_static_library(nwb_tool_cli)
target_sources(nwb_tool_cli PRIVATE "${CMAKE_CURRENT_LIST_DIR}/cli/tool_cli.cpp")
target_link_libraries(nwb_tool_cli PUBLIC nwb_common nwb::cli11_headers)
