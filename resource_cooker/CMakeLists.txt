nwb_declare_executable(nwb_resource_cooker)
target_sources(nwb_resource_cooker PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/command_line.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/command_line.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
)
nwb_target_link_libraries_whole_archive(nwb_resource_cooker
    nwb_assets_volume_cook
    nwb_assets_bunch_cook
    nwb_assets_texture_volume_entry
    nwb_assets_material_volume_entry
    nwb_assets_skeleton_volume_entry
    nwb_assets_mesh_volume_entries
    nwb_assets_model_volume_entry
    nwb_assets_graphics_cook
)
target_link_libraries(nwb_resource_cooker PRIVATE
    nwb_assets
    nwb_assets_cook
    nwb_common
    nwb_alloc
    nwb_filesystem
    nwb_logclient
    nwb::tbb
    nwb_metascript
    nwb::cli11_headers
)
target_compile_definitions(nwb_resource_cooker PRIVATE __TBB_NO_IMPLICIT_LINKAGE=1 __TBBMALLOC_NO_IMPLICIT_LINKAGE=1)
