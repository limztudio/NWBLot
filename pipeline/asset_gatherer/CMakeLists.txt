nwb_declare_executable(nwb_asset_gatherer)
target_sources(nwb_asset_gatherer PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
)
target_link_libraries(nwb_asset_gatherer PRIVATE nwb_pipeline_command_line nwb_assets_volume_cook nwb_assets_graphics_gather)
target_compile_definitions(nwb_asset_gatherer PRIVATE NWB_COOK=1 NWB_PIPELINE_TOOL_NAME="asset_gatherer")

