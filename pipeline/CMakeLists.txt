nwb_declare_static_library(nwb_pipeline_command_line)
target_sources(nwb_pipeline_command_line PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/command_line.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/command_line.h"
)
target_link_libraries(nwb_pipeline_command_line PUBLIC nwb_assets_cook nwb_common nwb::cli11_headers)
target_compile_definitions(nwb_pipeline_command_line PRIVATE NWB_COOK=1)

nwb_declare_static_library(nwb_pipeline_tools_common)
nwb_target_adhd_sources(nwb_pipeline_tools_common NAMESPACE "NWB::Pipeline" SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.adhd")
target_link_libraries(nwb_pipeline_tools_common PUBLIC nwb_assets_cook nwb_tool_cli)
target_compile_definitions(nwb_pipeline_tools_common PRIVATE NWB_COOK=1)

add_subdirectory(dependency_computer)
add_subdirectory(asset_builder)
add_subdirectory(asset_gatherer)
add_subdirectory(adhd_compiler)

add_custom_target(nwb_pipeline DEPENDS nwb_dependency_computer nwb_asset_builder nwb_asset_gatherer nwb_adhd_compiler)

