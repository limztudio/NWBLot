nwb_declare_static_library(nwb_pipeline_asset_builder)
target_sources(nwb_pipeline_asset_builder PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/build.h"
    "${CMAKE_CURRENT_LIST_DIR}/build.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/build_options.h"
    "${CMAKE_CURRENT_LIST_DIR}/build_inputs.h"
    "${CMAKE_CURRENT_LIST_DIR}/build_inputs.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/cook_paths.h"
    "${CMAKE_CURRENT_LIST_DIR}/cook_paths.cpp"
)
target_link_libraries(nwb_pipeline_asset_builder PUBLIC nwb_assets_volume_cook nwb_task)
target_compile_definitions(nwb_pipeline_asset_builder PRIVATE NWB_COOK=1)

nwb_declare_executable(nwb_asset_builder)
target_sources(nwb_asset_builder PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
)
nwb_target_link_libraries_whole_archive(nwb_asset_builder
    nwb_assets_volume_cook
    nwb_assets_bunch_cook
    nwb_assets_texture_volume_entry
    nwb_assets_sampler_volume_entry
    nwb_assets_material_volume_entry
    nwb_assets_skeleton_volume_entry
    nwb_assets_mesh_volume_entries
    nwb_assets_model_volume_entry
    nwb_assets_graphics_cook
)
target_link_libraries(nwb_asset_builder PRIVATE nwb_pipeline_asset_builder nwb_pipeline_command_line nwb::tbb)
target_compile_definitions(nwb_asset_builder PRIVATE NWB_COOK=1 NWB_PIPELINE_TOOL_NAME="asset_builder"
    __TBB_NO_IMPLICIT_LINKAGE=1 __TBBMALLOC_NO_IMPLICIT_LINKAGE=1)

