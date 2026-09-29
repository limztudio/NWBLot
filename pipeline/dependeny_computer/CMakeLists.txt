nwb_declare_executable(nwb_dependeny_computer)
target_sources(nwb_dependeny_computer PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/dependencies.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/dependencies.h"
    "${CMAKE_CURRENT_LIST_DIR}/provider_catalog.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/provider_catalog.h"
)
target_link_libraries(nwb_dependeny_computer PRIVATE nwb_pipeline_command_line nwb_assets_ui_skin_cook)
target_compile_definitions(nwb_dependeny_computer PRIVATE NWB_COOK=1 NWB_PIPELINE_TOOL_NAME="dependeny_computer")

