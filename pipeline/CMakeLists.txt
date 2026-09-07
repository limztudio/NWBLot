nwb_declare_static_library(nwb_pipeline_command_line)
target_sources(nwb_pipeline_command_line PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/command_line.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/command_line.h"
)
target_link_libraries(nwb_pipeline_command_line PUBLIC nwb_assets_cook nwb_logclient nwb::cli11_headers)
target_compile_definitions(nwb_pipeline_command_line PRIVATE NWB_COOK=1)

add_subdirectory(dependeny_computer)
add_subdirectory(asset_builder)
add_subdirectory(asset_gatherer)

add_custom_target(nwb_pipeline DEPENDS nwb_dependeny_computer nwb_asset_builder nwb_asset_gatherer)

