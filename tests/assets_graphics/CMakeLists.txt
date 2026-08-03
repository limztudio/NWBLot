nwb_declare_gtest_executable(nwb_assets_graphics_tests)
target_sources(nwb_assets_graphics_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/acceptance_tests.inl"
    "${CMAKE_CURRENT_LIST_DIR}/caustic_refract_tests.inl"
    "${CMAKE_CURRENT_LIST_DIR}/csg_cook_tests.inl"
    "${CMAKE_CURRENT_LIST_DIR}/codec_tests.inl"
    "${CMAKE_CURRENT_LIST_DIR}/material_cook_tests.inl"
    "${CMAKE_CURRENT_LIST_DIR}/material_tests.inl"
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_acceptance_helpers.inl"
    "${CMAKE_CURRENT_LIST_DIR}/meshlet_ref_codec_tests.inl"
    "${CMAKE_CURRENT_LIST_DIR}/model_fixture_tests.inl"
    "${CMAKE_CURRENT_LIST_DIR}/mesh_cooker_tests.inl"
    "${CMAKE_CURRENT_LIST_DIR}/shader_tests.inl"
    "${CMAKE_CURRENT_LIST_DIR}/texture_tests.inl"
    "${CMAKE_CURRENT_LIST_DIR}/volume_extensibility_tests.inl"
    "${CMAKE_CURRENT_LIST_DIR}/../meshlet_ref_test_data.h"
)
target_compile_definitions(nwb_assets_graphics_tests PRIVATE NWB_COOK=1)
nwb_target_link_libraries_whole_archive(nwb_assets_graphics_tests
    nwb_assets_volume_cook
    nwb_assets_bunch_cook
    nwb_assets_material_volume_entry
    nwb_assets_skeleton_volume_entry
    nwb_assets_mesh_volume_entries
    nwb_assets_model_volume_entry
    nwb_assets_texture_volume_entry
    nwb_assets_graphics_cook
)
target_link_libraries(nwb_assets_graphics_tests PRIVATE
    nwb_ecs_csg
    nwb_assets_cook
    nwb_shader_archive
    nwb_graphics
    nwb_common
    nwb_alloc
)
set_tests_properties(nwb_assets_graphics_tests PROPERTIES RUN_SERIAL TRUE)
