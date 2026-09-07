nwb_declare_executable(nwb_dependeny_computer)
target_sources(nwb_dependeny_computer PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
)
target_link_libraries(nwb_dependeny_computer PRIVATE nwb_pipeline_command_line)
target_compile_definitions(nwb_dependeny_computer PRIVATE NWB_COOK=1 NWB_PIPELINE_TOOL_NAME="dependeny_computer")

