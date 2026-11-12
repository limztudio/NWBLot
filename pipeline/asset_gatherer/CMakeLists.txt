file(GLOB _nwb_pipeline_asset_gatherer_adhd_sources CONFIGURE_DEPENDS LIST_DIRECTORIES false "${CMAKE_CURRENT_LIST_DIR}/*.adhd")
list(REMOVE_ITEM _nwb_pipeline_asset_gatherer_adhd_sources "${CMAKE_CURRENT_LIST_DIR}/module.adhd")

nwb_declare_static_library(nwb_pipeline_asset_gatherer)
nwb_target_adhd_sources(nwb_pipeline_asset_gatherer NAMESPACE "NWB::Pipeline::AssetGatherer" SOURCES ${_nwb_pipeline_asset_gatherer_adhd_sources})
unset(_nwb_pipeline_asset_gatherer_adhd_sources)
target_link_libraries(nwb_pipeline_asset_gatherer PUBLIC nwb_assets_volume_cook)
target_compile_definitions(nwb_pipeline_asset_gatherer PRIVATE NWB_COOK=1)

nwb_declare_executable(nwb_asset_gatherer)
target_sources(nwb_asset_gatherer PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../main.cpp"
)
nwb_target_adhd_sources(nwb_asset_gatherer NAMESPACE "NWB::Pipeline" SOURCES "${CMAKE_CURRENT_LIST_DIR}/module.adhd")
nwb_target_link_libraries_whole_archive(nwb_asset_gatherer nwb_assets_graphics_gather)
target_link_libraries(nwb_asset_gatherer PRIVATE nwb_pipeline_command_line nwb_pipeline_asset_gatherer nwb_logclient)
target_compile_definitions(nwb_asset_gatherer PRIVATE NWB_COOK=1 NWB_PIPELINE_TOOL_NAME="asset_gatherer")
