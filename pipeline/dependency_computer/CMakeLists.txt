nwb_declare_executable(nwb_dependency_computer)
target_sources(nwb_dependency_computer PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/*.cxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/*.ixx"
)
nwb_target_link_libraries_whole_archive(nwb_dependency_computer
    nwb_assets_bunch_cook
    nwb_assets_csg_volume_entry
    nwb_assets_font_atlas_volume_entry
    nwb_assets_material_volume_entry
    nwb_assets_mesh_volume_entries
    nwb_assets_model_volume_entry
    nwb_assets_scene_volume_entry
    nwb_assets_graphics_volume_entry
    nwb_assets_ui_skin_volume_entry
)
get_property(_nwb_registered_pipeline_volume_entries GLOBAL PROPERTY NWB_PIPELINE_VOLUME_ENTRY_TARGETS)
if(_nwb_registered_pipeline_volume_entries)
    nwb_target_link_libraries_whole_archive(nwb_dependency_computer ${_nwb_registered_pipeline_volume_entries})
endif()
target_link_libraries(nwb_dependency_computer PRIVATE
    nwb_pipeline_command_line
    nwb_assets_cook
    nwb_logclient
    nwb::tbb
)
target_compile_definitions(nwb_dependency_computer PRIVATE NWB_COOK=1 NWB_PIPELINE_TOOL_NAME="dependency_computer" __TBB_NO_IMPLICIT_LINKAGE=1 __TBBMALLOC_NO_IMPLICIT_LINKAGE=1)
