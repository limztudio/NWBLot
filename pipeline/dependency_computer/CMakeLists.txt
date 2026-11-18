file(GLOB _nwb_pipeline_dependency_computer_adhd_sources CONFIGURE_DEPENDS LIST_DIRECTORIES false "${CMAKE_CURRENT_LIST_DIR}/*.adhd")
list(REMOVE_ITEM _nwb_pipeline_dependency_computer_adhd_sources "${CMAKE_CURRENT_LIST_DIR}/module.adhd")

nwb_declare_static_library(nwb_pipeline_dependency_computer)
nwb_target_adhd_sources(nwb_pipeline_dependency_computer NAMESPACE "NWB::Pipeline::DependencyComputer" SOURCES ${_nwb_pipeline_dependency_computer_adhd_sources})
unset(_nwb_pipeline_dependency_computer_adhd_sources)
target_link_libraries(nwb_pipeline_dependency_computer PUBLIC nwb_pipeline_tools_common nwb_assets_bunch_cook nwb_assets_cook)
target_compile_definitions(nwb_pipeline_dependency_computer PRIVATE NWB_COOK=1)

nwb_declare_executable(nwb_dependency_computer)
nwb_target_adhd_sources(nwb_dependency_computer NAMESPACE "NWB::Pipeline" SOURCES "${CMAKE_CURRENT_LIST_DIR}/module.adhd")
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
target_link_libraries(nwb_dependency_computer PRIVATE
    nwb_pipeline_dependency_computer
    nwb_logclient
)
target_compile_definitions(nwb_dependency_computer PRIVATE NWB_COOK=1 __TBB_NO_IMPLICIT_LINKAGE=1 __TBBMALLOC_NO_IMPLICIT_LINKAGE=1)

nwb_target_console_entry(nwb_dependency_computer "pipeline/dependency_computer/module.adhd.h" "NWB::Pipeline::Run" "dependency_computer" "NWB::Pipeline::s_PipelineExitFatal")
