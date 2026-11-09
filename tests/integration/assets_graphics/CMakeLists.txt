nwb_declare_gtest_executable(nwb_assets_graphics_tests)
nwb_target_adhd_sources(nwb_assets_graphics_tests NAMESPACE "NWB::Tests::AssetsGraphicsFixture" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/case_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/suite.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/assets_graphics_bunch_and_cache_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/assets_graphics_reload_and_csg_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/assets_graphics_material_and_mesh_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/assets_graphics_metadata_and_model_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/assets_graphics_sampler_and_shader_cook_cases.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/assets_graphics_shader_and_texture_cases.adhd"
)
target_compile_definitions(nwb_assets_graphics_tests PRIVATE NWB_COOK=1 NWB_REPO_ROOT="${CMAKE_SOURCE_DIR}")
nwb_target_link_libraries_whole_archive(nwb_assets_graphics_tests nwb_assets_volume_cook nwb_assets_bunch_cook nwb_assets_material_volume_entry nwb_assets_skeleton_volume_entry nwb_assets_mesh_volume_entries nwb_assets_model_volume_entry
    nwb_assets_font_atlas_volume_entry nwb_assets_font_volume_entry nwb_assets_texture_volume_entry nwb_assets_ui_skin_volume_entry nwb_assets_sampler_volume_entry nwb_assets_graphics_volume_entry
    nwb_assets_scene_volume_entry nwb_assets_graphics_cook nwb_assets_graphics_gather)
target_link_libraries(nwb_assets_graphics_tests PRIVATE nwb_pipeline_asset_builder nwb_pipeline_asset_gatherer nwb_ecs_csg nwb_assets_cook nwb_assets_texture_loader nwb_assets_texture_cook nwb_shader_archive nwb_graphics nwb_common nwb_alloc)
# Run serially and allow extra time for repeated Slang shader cooks.
set(_nwb_assets_graphics_tests_timeout_seconds 3600)
set_tests_properties(nwb_assets_graphics_tests PROPERTIES RUN_SERIAL TRUE TIMEOUT ${_nwb_assets_graphics_tests_timeout_seconds})
unset(_nwb_assets_graphics_tests_timeout_seconds)
