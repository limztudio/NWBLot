nwb_declare_static_library(nwb_pipeline_asset_gatherer)
target_sources(nwb_pipeline_asset_gatherer PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/gather.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/gather.h"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
)
target_link_libraries(nwb_pipeline_asset_gatherer PUBLIC nwb_assets_volume_cook)
target_compile_definitions(nwb_pipeline_asset_gatherer PRIVATE NWB_COOK=1)

nwb_declare_executable(nwb_asset_gatherer)
target_sources(nwb_asset_gatherer PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
)
target_link_libraries(nwb_asset_gatherer PRIVATE nwb_pipeline_command_line nwb_pipeline_asset_gatherer nwb_assets_graphics_gather)
target_compile_definitions(nwb_asset_gatherer PRIVATE NWB_COOK=1 NWB_PIPELINE_TOOL_NAME="asset_gatherer")

