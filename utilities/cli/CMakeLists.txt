nwb_declare_static_library(nwb_tool_cli)
nwb_target_adhd_sources(nwb_tool_cli NAMESPACE "NWB::Utilities" SOURCES "${CMAKE_CURRENT_LIST_DIR}/tool_cli.adhd")
target_sources(nwb_tool_cli PRIVATE "${CMAKE_CURRENT_LIST_DIR}/tool_cli.cpp")
target_link_libraries(nwb_tool_cli PUBLIC nwb_common nwb::cli11_headers)
