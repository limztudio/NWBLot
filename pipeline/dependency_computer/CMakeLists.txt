nwb_declare_executable(nwb_dependency_computer)
target_sources(nwb_dependency_computer PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/dependencies.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/dependencies.h"
    "${CMAKE_CURRENT_LIST_DIR}/provider_catalog.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/provider_catalog.h"
)
target_link_libraries(nwb_dependency_computer PRIVATE nwb_pipeline_command_line nwb_assets_ui_skin_cook nwb_logclient)
target_compile_definitions(nwb_dependency_computer PRIVATE NWB_COOK=1 NWB_PIPELINE_TOOL_NAME="dependency_computer")

