nwb_declare_gtest_executable(nwb_assets_graphics_tests)
target_sources(nwb_assets_graphics_tests PRIVATE
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
target_compile_definitions(nwb_assets_graphics_tests PRIVATE NWB_COOK=1)
nwb_target_link_libraries_whole_archive(nwb_assets_graphics_tests nwb_assets_volume_cook nwb_assets_bunch_cook nwb_assets_material_volume_entry nwb_assets_skeleton_volume_entry nwb_assets_mesh_volume_entries nwb_assets_model_volume_entry
    nwb_assets_font_atlas_volume_entry nwb_assets_font_volume_entry nwb_assets_texture_volume_entry nwb_assets_ui_skin_volume_entry nwb_assets_sampler_volume_entry nwb_assets_graphics_volume_entry nwb_assets_scene_volume_entry nwb_assets_graphics_cook nwb_assets_graphics_gather)
target_link_libraries(nwb_assets_graphics_tests PRIVATE nwb_pipeline_asset_builder nwb_pipeline_asset_gatherer nwb_ecs_csg nwb_assets_cook nwb_assets_texture_loader nwb_shader_archive nwb_graphics nwb_common nwb_alloc)
# Run serially and allow extra time for repeated Slang shader cooks.
set(_nwb_assets_graphics_tests_timeout_seconds 3600)
set_tests_properties(nwb_assets_graphics_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT ${_nwb_assets_graphics_tests_timeout_seconds})
unset(_nwb_assets_graphics_tests_timeout_seconds)
